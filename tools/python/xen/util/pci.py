#!/usr/bin/env python
#
# PCI Device Information Class
# - Helps obtain information about which I/O resources a PCI device needs
#
#   Author: Ryan Wilson <hap9@epoch.ncsc.mil>

import sys
import os, os.path
import resource

PROC_MNT_PATH = '/proc/mounts'
PROC_PCI_PATH = '/proc/bus/pci/devices'
PROC_PCI_NUM_RESOURCES = 7

SYSFS_PCI_DEVS_PATH = '/bus/pci/devices'
SYSFS_PCI_DEV_RESOURCE_PATH = '/resource'
SYSFS_PCI_DEV_CONFIG_PATH = '/config'
SYSFS_PCI_DEV_IRQ_PATH = '/irq'
SYSFS_PCI_DEV_DRIVER_DIR_PATH = '/driver'
SYSFS_PCI_DEV_VENDOR_PATH = '/vendor'
SYSFS_PCI_DEV_DEVICE_PATH = '/device'
SYSFS_PCI_DEV_SUBVENDOR_PATH = '/subsystem_vendor'
SYSFS_PCI_DEV_SUBDEVICE_PATH = '/subsystem_device'

PCI_BAR_IO = 0x01
PCI_BAR_IO_MASK = ~0x03
PCI_BAR_MEM_MASK = ~0x0f
PCI_STATUS_CAP_MASK = 0x10
PCI_STATUS_OFFSET = 0x6
PCI_CAP_OFFSET = 0x34
MSIX_BIR_MASK = 0x7

#Calculate PAGE_SHIFT: number of bits to shift an address to get the page number
PAGE_SIZE = resource.getpagesize()
PAGE_SHIFT = 0
t = PAGE_SIZE
while not (t&1):
    t>>=1
    PAGE_SHIFT+=1

PAGE_MASK=~(PAGE_SIZE - 1)
# Definitions from Linux: include/linux/pci.h
def PCI_DEVFN(slot, func):
    return ((((slot) & 0x1f) << 3) | ((func) & 0x07))

def find_sysfs_mnt():
    mounts_file = open(PROC_MNT_PATH,'r')

    for line in mounts_file:
        sline = line.split()
        if len(sline)<3:
            continue

        if sline[2]=='sysfs':
            return sline[1]

    return None

class PciDeviceNotFoundError(Exception):
    def __init__(self,domain,bus,slot,func):
        self.domain = domain
        self.bus = bus
        self.slot = slot
        self.func = func
        self.name = "%04x:%02x:%02x.%01x"%(domain, bus, slot, func)
    
    def __str__(self):
        return ('PCI Device %s Not Found' % (self.name))

class PciDeviceParseError(Exception):
    def __init__(self,msg):
        self.message = msg
    def __str__(self):
        return 'Error Parsing PCI Device Info: '+self.message

class PciDevice:
    def __init__(self, domain, bus, slot, func):
        self.domain = domain
        self.bus = bus
        self.slot = slot
        self.func = func
        self.name = "%04x:%02x:%02x.%01x"%(domain, bus, slot, func)
        self.irq = 0
        self.iomem = []
        self.ioports = []
        self.driver = None
        self.vendor = None
        self.device = None
        self.subvendor = None
        self.subdevice = None
        self.msix = 0
        self.msix_iomem = []
        self.get_info_from_sysfs()

    def find_capability(self, type):
        try:
            sysfs_mnt = find_sysfs_mnt()
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to locate sysfs mount: %s (%d)' %
                (PROC_PCI_PATH, strerr, errno)))

        if sysfs_mnt == None:
            return False
        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
               self.name+SYSFS_PCI_DEV_CONFIG_PATH
        try:
            conf_file = open(path, 'rb')
            conf_file.seek(PCI_STATUS_OFFSET)
            status = ord(conf_file.read(1))
            if status&PCI_STATUS_CAP_MASK:
                conf_file.seek(PCI_CAP_OFFSET)
                capa_pointer = ord(conf_file.read(1))
                while capa_pointer:
                    conf_file.seek(capa_pointer)
                    capa_id = ord(conf_file.read(1))
                    capa_pointer = ord(conf_file.read(1))
                    if capa_id == type:
                        # get the type
                        message_cont_lo = ord(conf_file.read(1))
                        message_cont_hi = ord(conf_file.read(1))
                        self.msix=1
                        self.msix_entries = message_cont_lo + \
                                            message_cont_hi << 8
                        t_off=conf_file.read(4)
                        p_off=conf_file.read(4)
                        self.table_offset=ord(t_off[0]) | (ord(t_off[1])<<8) | \
                                          (ord(t_off[2])<<16)|  \
                                          (ord(t_off[3])<<24)
                        self.pba_offset=ord(p_off[0]) | (ord(p_off[1]) << 8)| \
                                        (ord(p_off[2])<<16) | \
                                        (ord(p_off[3])<<24)
                        self.table_index = self.table_offset & MSIX_BIR_MASK
                        self.table_offset = self.table_offset & ~MSIX_BIR_MASK
                        self.pba_index = self.pba_offset & MSIX_BIR_MASK
                        self.pba_offset = self.pba_offset & ~MSIX_BIR_MASK
                        break
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to locate sysfs mount: %s (%d)' %
                (PROC_PCI_PATH, strerr, errno)))

    def remove_msix_iomem(self, index, start, size):
        if (index == self.table_index):
            table_start = start+self.table_offset
            table_end = table_start + self.msix_entries * 16
            table_start = table_start & PAGE_MASK
            table_end = (table_end + PAGE_SIZE) & PAGE_MASK
            self.msix_iomem.append((table_start, table_end-table_start))
        if (index==self.pba_index):
            pba_start = start + self.pba_offset
            pba_end = pba_start + self.msix_entries/8
            pba_start = pba_start & PAGE_MASK
            pba_end = (pba_end + PAGE_SIZE) & PAGE_MASK
            self.msix_iomem.append((pba_start, pba_end-pba_start))

    def get_info_from_sysfs(self):
        self.find_capability(0x11)
        try:
            sysfs_mnt = find_sysfs_mnt()
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to locate sysfs mount: %s (%d)' %
                (PROC_PCI_PATH, strerr, errno)))

        if sysfs_mnt == None:
            return False

        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
                self.name+SYSFS_PCI_DEV_RESOURCE_PATH
        try:
            resource_file = open(path,'r')

            for i in range(PROC_PCI_NUM_RESOURCES):
                line = resource_file.readline()
                sline = line.split()
                if len(sline)<3:
                    continue

                start = int(sline[0],16)
                end = int(sline[1],16)
                flags = int(sline[2],16)
                size = end-start+1

                if start!=0:
                    if flags&PCI_BAR_IO:
                        self.ioports.append( (start,size) )
                    else:
                        self.iomem.append( (start,size) )
                    if (self.msix):
                        self.remove_msix_iomem(i, start, size)



        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to open & read %s: %s (%d)' %
                (path, strerr, errno)))

        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
                self.name+SYSFS_PCI_DEV_IRQ_PATH
        try:
            self.irq = int(open(path,'r').readline())
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to open & read %s: %s (%d)' %
                (path, strerr, errno)))

        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
                self.name+SYSFS_PCI_DEV_DRIVER_DIR_PATH
        try:
            self.driver = os.path.basename(os.readlink(path))
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to read %s: %s (%d)' %
                (path, strerr, errno)))

        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
                self.name+SYSFS_PCI_DEV_VENDOR_PATH
        try:
            self.vendor = int(open(path,'r').readline(), 16)
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to open & read %s: %s (%d)' %
                (path, strerr, errno)))

        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
                self.name+SYSFS_PCI_DEV_DEVICE_PATH
        try:
            self.device = int(open(path,'r').readline(), 16)
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to open & read %s: %s (%d)' %
                (path, strerr, errno)))

        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
                self.name+SYSFS_PCI_DEV_SUBVENDOR_PATH
        try:
            self.subvendor = int(open(path,'r').readline(), 16)
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to open & read %s: %s (%d)' %
                (path, strerr, errno)))

        path = sysfs_mnt+SYSFS_PCI_DEVS_PATH+'/'+ \
                self.name+SYSFS_PCI_DEV_SUBDEVICE_PATH
        try:
            self.subdevice = int(open(path,'r').readline(), 16)
        except IOError, (errno, strerr):
            raise PciDeviceParseError(('Failed to open & read %s: %s (%d)' %
                (path, strerr, errno)))

        return True

    def __str__(self):
        str = "PCI Device %s\n" % (self.name)
        for (start,size) in self.ioports:
            str = str + "IO Port 0x%02x [size=%d]\n"%(start,size)
        for (start,size) in self.iomem:
            str = str + "IO Mem 0x%02x [size=%d]\n"%(start,size)
        str = str + "IRQ %d\n"%(self.irq)
        str = str + "Vendor ID 0x%04x\n"%(self.vendor)
        str = str + "Device ID 0x%04x\n"%(self.device)
        str = str + "Sybsystem Vendor ID 0x%04x\n"%(self.subvendor)
        str = str + "Subsystem Device ID 0x%04x"%(self.subdevice)
        return str

def main():
    if len(sys.argv)<5:
        print "Usage: %s <domain> <bus> <slot> <func>\n"
        sys.exit(2)

    dev = PciDevice(int(sys.argv[1],16), int(sys.argv[2],16),
            int(sys.argv[3],16), int(sys.argv[4],16))
    print str(dev)

if __name__=='__main__':
    main()
