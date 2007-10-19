#============================================================================
# This library is free software; you can redistribute it and/or
# modify it under the terms of version 2.1 of the GNU Lesser General Public
# License as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#============================================================================
# Copyright (C) 2005 Mike Wray <mike.wray@hp.com>
# Copyright (C) 2005-2007 XenSource Ltd
#============================================================================


import os, string
import re
import math
import time
import signal

import xen.lowlevel.xc
from xen.xend.XendConstants import *
from xen.xend.XendError import VmError, XendError, HVMRequired
from xen.xend.XendLogging import log
from xen.xend.XendOptions import instance as xenopts
from xen.xend.xenstore.xstransact import xstransact
from xen.xend.xenstore.xswatch import xswatch
from xen.xend import arch

xc = xen.lowlevel.xc.xc()

MAX_GUEST_CMDLINE = 1024


def create(vm, vmConfig):
    """Create an image handler for a vm.

    @return ImageHandler instance
    """
    return findImageHandlerClass(vmConfig)(vm, vmConfig)


class ImageHandler:
    """Abstract base class for image handlers.

    createImage() is called to configure and build the domain from its
    kernel image and ramdisk etc.

    The method buildDomain() is used to build the domain, and must be
    defined in a subclass.  Usually this is the only method that needs
    defining in a subclass.

    The method createDeviceModel() is called to create the domain device
    model if it needs one.  The default is to do nothing.

    The method destroy() is called when the domain is destroyed.
    The default is to do nothing.
    """

    ostype = None


    def __init__(self, vm, vmConfig):
        self.vm = vm

        self.bootloader = False
        self.kernel = None
        self.ramdisk = None
        self.cmdline = None

        self.configure(vmConfig)

    def configure(self, vmConfig):
        """Config actions common to all unix-like domains."""
        if '_temp_using_bootloader' in vmConfig:
            self.bootloader = True
            self.kernel = vmConfig['_temp_kernel']
            self.cmdline = vmConfig['_temp_args']
            self.ramdisk = vmConfig['_temp_ramdisk']
        else:
            self.kernel = vmConfig['PV_kernel']
            self.cmdline = vmConfig['PV_args']
            self.ramdisk = vmConfig['PV_ramdisk']
        self.vm.storeVm(("image/ostype", self.ostype),
                        ("image/kernel", self.kernel),
                        ("image/cmdline", self.cmdline),
                        ("image/ramdisk", self.ramdisk))


    def cleanupBootloading(self):
        if self.bootloader:
            self.unlink(self.kernel)
            self.unlink(self.ramdisk)


    def unlink(self, f):
        if not f: return
        try:
            os.unlink(f)
        except OSError, ex:
            log.warning("error removing bootloader file '%s': %s", f, ex)


    def createImage(self):
        """Entry point to create domain memory image.
        Override in subclass  if needed.
        """
        return self.createDomain()


    def createDomain(self):
        """Build the domain boot image.
        """
        # Set params and call buildDomain().

        if not os.path.isfile(self.kernel):
            raise VmError('Kernel image does not exist: %s' % self.kernel)
        if self.ramdisk and not os.path.isfile(self.ramdisk):
            raise VmError('Kernel ramdisk does not exist: %s' % self.ramdisk)
        if len(self.cmdline) >= MAX_GUEST_CMDLINE:
            log.warning('kernel cmdline too long, domain %d',
                        self.vm.getDomid())
        
        log.info("buildDomain os=%s dom=%d vcpus=%d", self.ostype,
                 self.vm.getDomid(), self.vm.getVCpuCount())

        result = self.buildDomain()

        if isinstance(result, dict):
            return result
        else:
            raise VmError('Building domain failed: ostype=%s dom=%d err=%s'
                          % (self.ostype, self.vm.getDomid(), str(result)))

    def getRequiredAvailableMemory(self, mem_kb):
        """@param mem_kb The configured maxmem or memory, in KiB.
        @return The corresponding required amount of memory for the domain,
        also in KiB.  This is normally the given mem_kb, but architecture- or
        image-specific code may override this to add headroom where
        necessary."""
        return mem_kb

    def getRequiredInitialReservation(self):
        """@param mem_kb The configured memory, in KiB.
        @return The corresponding required amount of memory to be free, also
        in KiB. This is normally the same as getRequiredAvailableMemory, but
        architecture- or image-specific code may override this to
        add headroom where necessary."""
        return self.getRequiredAvailableMemory(self.vm.getMemoryTarget())

    def getRequiredMaximumReservation(self):
        """@param mem_kb The maximum possible memory, in KiB.
        @return The corresponding required amount of memory to be free, also
        in KiB. This is normally the same as getRequiredAvailableMemory, but
        architecture- or image-specific code may override this to
        add headroom where necessary."""
        return self.getRequiredAvailableMemory(self.vm.getMemoryMaximum())

    def getRequiredShadowMemory(self, shadow_mem_kb, maxmem_kb):
        """@param shadow_mem_kb The configured shadow memory, in KiB.
        @param maxmem_kb The configured maxmem, in KiB.
        @return The corresponding required amount of shadow memory, also in
        KiB."""
        # PV domains don't need any shadow memory
        return 0

    def buildDomain(self):
        """Build the domain. Define in subclass."""
        raise NotImplementedError()

    def createDeviceModel(self, restore = False):
        """Create device model for the domain (define in subclass if needed)."""
        pass
    
    def saveDeviceModel(self):
        """Save device model for the domain (define in subclass if needed)."""
        pass

    def resumeDeviceModel(self):
        """Unpause device model for the domain (define in subclass if needed)."""
        pass

    def destroy(self):
        """Extra cleanup on domain destroy (define in subclass if needed)."""
        pass


    def recreate(self):
        pass


class LinuxImageHandler(ImageHandler):

    ostype = "linux"
    flags = 0

    def buildDomain(self):
        store_evtchn = self.vm.getStorePort()
        console_evtchn = self.vm.getConsolePort()

        mem_mb = self.getRequiredInitialReservation() / 1024

        log.debug("domid          = %d", self.vm.getDomid())
        log.debug("memsize        = %d", mem_mb)
        log.debug("image          = %s", self.kernel)
        log.debug("store_evtchn   = %d", store_evtchn)
        log.debug("console_evtchn = %d", console_evtchn)
        log.debug("cmdline        = %s", self.cmdline)
        log.debug("ramdisk        = %s", self.ramdisk)
        log.debug("vcpus          = %d", self.vm.getVCpuCount())
        log.debug("features       = %s", self.vm.getFeatures())
        if arch.type == "ia64":
            log.debug("vhpt          = %d", self.flags)

        return xc.linux_build(domid          = self.vm.getDomid(),
                              memsize        = mem_mb,
                              image          = self.kernel,
                              store_evtchn   = store_evtchn,
                              console_evtchn = console_evtchn,
                              cmdline        = self.cmdline,
                              ramdisk        = self.ramdisk,
                              features       = self.vm.getFeatures(),
                              flags          = self.flags)

class PPC_LinuxImageHandler(LinuxImageHandler):

    ostype = "linux"
    
    def getRequiredShadowMemory(self, shadow_mem_kb, maxmem_kb):
        """@param shadow_mem_kb The configured shadow memory, in KiB.
        @param maxmem_kb The configured maxmem, in KiB.
        @return The corresponding required amount of shadow memory, also in
        KiB.
        PowerPC currently uses "shadow memory" to refer to the hash table."""
        return max(maxmem_kb / 64, shadow_mem_kb)



class HVMImageHandler(ImageHandler):

    ostype = "hvm"

    def __init__(self, vm, vmConfig):
        ImageHandler.__init__(self, vm, vmConfig)
        self.shutdownWatch = None
        self.rebootFeatureWatch = None

    def configure(self, vmConfig):
        ImageHandler.configure(self, vmConfig)

        if not self.kernel:
            self.kernel = '/usr/lib/xen/boot/hvmloader'

        info = xc.xeninfo()
        if 'hvm' not in info['xen_caps']:
            raise HVMRequired()

        self.dmargs = self.parseDeviceModelArgs(vmConfig)
        self.device_model = vmConfig['platform'].get('device_model')
        if not self.device_model:
            raise VmError("hvm: missing device model")
        
        self.display = vmConfig['platform'].get('display')
        self.xauthority = vmConfig['platform'].get('xauthority')
        self.vncconsole = vmConfig['platform'].get('vncconsole')

        rtc_timeoffset = vmConfig['platform'].get('rtc_timeoffset')

        self.vm.storeVm(("image/dmargs", " ".join(self.dmargs)),
                        ("image/device-model", self.device_model),
                        ("image/display", self.display))
        self.vm.storeVm(("rtc/timeoffset", rtc_timeoffset))

        self.pid = None

        self.apic = int(vmConfig['platform'].get('apic', 0))
        self.acpi = int(vmConfig['platform'].get('acpi', 0))
        

    def buildDomain(self):
        store_evtchn = self.vm.getStorePort()

        mem_mb = self.getRequiredInitialReservation() / 1024

        log.debug("domid          = %d", self.vm.getDomid())
        log.debug("image          = %s", self.kernel)
        log.debug("store_evtchn   = %d", store_evtchn)
        log.debug("memsize        = %d", mem_mb)
        log.debug("vcpus          = %d", self.vm.getVCpuCount())
        log.debug("acpi           = %d", self.acpi)
        log.debug("apic           = %d", self.apic)

        rc = xc.hvm_build(domid          = self.vm.getDomid(),
                          image          = self.kernel,
                          memsize        = mem_mb,
                          vcpus          = self.vm.getVCpuCount(),
                          acpi           = self.acpi,
                          apic           = self.apic)

        rc['notes'] = { 'SUSPEND_CANCEL': 1 }

        rc['store_mfn'] = xc.hvm_get_param(self.vm.getDomid(),
                                           HVM_PARAM_STORE_PFN)
        xc.hvm_set_param(self.vm.getDomid(), HVM_PARAM_STORE_EVTCHN,
                         store_evtchn)

        return rc

    # Return a list of cmd line args to the device models based on the
    # xm config file
    def parseDeviceModelArgs(self, vmConfig):
        dmargs = [ 'boot', 'fda', 'fdb', 'soundhw',
                   'localtime', 'serial', 'stdvga', 'isa',
                   'acpi', 'usb', 'usbdevice', 'keymap', 'pci' ]
        
        ret = ['-vcpus', str(self.vm.getVCpuCount())]

        for a in dmargs:
            v = vmConfig['platform'].get(a)

            # python doesn't allow '-' in variable names
            if a == 'stdvga': a = 'std-vga'
            if a == 'keymap': a = 'k'

            # Handle booleans gracefully
            if a in ['localtime', 'std-vga', 'isa', 'usb', 'acpi']:
                try:
                    if v != None: v = int(v)
                    if v: ret.append("-%s" % a)
                except (ValueError, TypeError):
                    pass # if we can't convert it to a sane type, ignore it
            else:
                if v:
                    ret.append("-%s" % a)
                    ret.append("%s" % v)

            if a in ['fda', 'fdb']:
                if v:
                    if not os.path.isabs(v):
                        raise VmError("Floppy file %s does not exist." % v)
            log.debug("args: %s, val: %s" % (a,v))

        # Handle disk/network related options
        mac = None
        ret = ret + ["-domain-name", str(self.vm.info['name_label'])]
        nics = 0
        
        for devuuid in vmConfig['vbd_refs']:
            devinfo = vmConfig['devices'][devuuid][1]
            uname = devinfo.get('uname')
            if uname is not None and 'file:' in uname:
                (_, vbdparam) = string.split(uname, ':', 1)
                if not os.path.isfile(vbdparam):
                    raise VmError('Disk image does not exist: %s' %
                                  vbdparam)

        for devuuid in vmConfig['vif_refs']:
            devinfo = vmConfig['devices'][devuuid][1]
            dtype = devinfo.get('type', 'ioemu')
            if dtype != 'ioemu':
                continue
            nics += 1
            mac = devinfo.get('mac')
            if mac is None:
                raise VmError("MAC address not specified or generated.")
            bridge = devinfo.get('bridge', 'xenbr0')
            model = devinfo.get('model', 'rtl8139')
            ret.append("-net")
            ret.append("nic,vlan=%d,macaddr=%s,model=%s" %
                       (nics, mac, model))
            ret.append("-net")
            ret.append("tap,vlan=%d,bridge=%s" % (nics, bridge))


        #
        # Find RFB console device, and if it exists, make QEMU enable
        # the VNC console.
        #
        if int(vmConfig['platform'].get('nographic', 0)) != 0:
            # skip vnc init if nographic is set
            ret.append('-nographic')
            return ret

        vnc_config = {}
        has_vnc = int(vmConfig['platform'].get('vnc', 0)) != 0
        has_sdl = int(vmConfig['platform'].get('sdl', 0)) != 0
        for dev_uuid in vmConfig['console_refs']:
            dev_type, dev_info = vmConfig['devices'][dev_uuid]
            if dev_type == 'vfb':
                vnc_config = dev_info.get('other_config', {})
                has_vnc = True
                break

        if has_vnc:
            if not vnc_config:
                for key in ('vncunused', 'vnclisten', 'vncdisplay',
                            'vncpasswd'):
                    if key in vmConfig['platform']:
                        vnc_config[key] = vmConfig['platform'][key]

            vnclisten = vnc_config.get('vnclisten',
                                       xenopts().get_vnclisten_address())
            vncdisplay = vnc_config.get('vncdisplay', 0)
            ret.append('-vnc')
            ret.append("%s:%d" % (vnclisten, vncdisplay))
            
            if vnc_config.get('vncunused', 0):
                ret.append('-vncunused')

            # Store vncpassword in xenstore
            vncpasswd = vnc_config.get('vncpasswd')
            if not vncpasswd:
                vncpasswd = xenopts().get_vncpasswd_default()

            if vncpasswd is None:
                raise VmError('vncpasswd is not setup in vmconfig or '
                              'xend-config.sxp')

            if vncpasswd != '':
                self.vm.storeVm('vncpasswd', vncpasswd)
        elif has_sdl:
            # SDL is default in QEMU.
            pass
        else:
            ret.append('-nographic')

        if int(vmConfig['platform'].get('monitor', 0)) != 0:
            ret = ret + ['-monitor', 'vc']
        return ret

    def createDeviceModel(self, restore = False):
        if self.pid:
            return
        # Execute device model.
        #todo: Error handling
        args = [self.device_model]
        args = args + ([ "-d",  "%d" % self.vm.getDomid() ])
        if arch.type == "ia64":
            args = args + ([ "-m", "%s" %
                             (self.getRequiredInitialReservation() / 1024) ])
        args = args + self.dmargs
        if restore:
            args = args + ([ "-loadvm", "/var/lib/xen/qemu-save.%d" %
                             self.vm.getDomid() ])
        env = dict(os.environ)
        if self.display:
            env['DISPLAY'] = self.display
        if self.xauthority:
            env['XAUTHORITY'] = self.xauthority
        if self.vncconsole:
            args = args + ([ "-vncviewer" ])
        log.info("spawning device models: %s %s", self.device_model, args)
        # keep track of pid and spawned options to kill it later
        self.pid = os.spawnve(os.P_NOWAIT, self.device_model, args, env)
        self.vm.storeDom("image/device-model-pid", self.pid)
        log.info("device model pid: %d", self.pid)

    def saveDeviceModel(self):
        # Signal the device model to pause itself and save its state
        xstransact.Store("/local/domain/0/device-model/%i"
                         % self.vm.getDomid(), ('command', 'save'))
        # Wait for confirmation.  Could do this with a watch but we'd
        # still end up spinning here waiting for the watch to fire. 
        state = ''
        count = 0
        while state != 'paused':
            state = xstransact.Read("/local/domain/0/device-model/%i/state"
                                    % self.vm.getDomid())
            time.sleep(0.1)
            count += 1
            if count > 100:
                raise VmError('Timed out waiting for device model to save')

    def resumeDeviceModel(self):
        # Signal the device model to resume activity after pausing to save.
        xstransact.Store("/local/domain/0/device-model/%i"
                         % self.vm.getDomid(), ('command', 'continue'))

    def recreate(self):
        self.pid = self.vm.gatherDom(('image/device-model-pid', int))

    def destroy(self, suspend = False):
        if self.pid and not suspend:
            try:
                os.kill(self.pid, signal.SIGKILL)
            except OSError, exn:
                log.exception(exn)
            try:
                os.waitpid(self.pid, 0)
            except OSError, exn:
                # This is expected if Xend has been restarted within the
                # life of this domain.  In this case, we can kill the process,
                # but we can't wait for it because it's not our child.
                pass
            self.pid = None
            state = xstransact.Remove("/local/domain/0/device-model/%i"
                                      % self.vm.getDomid())


class IA64_HVM_ImageHandler(HVMImageHandler):

    def configure(self, vmConfig):
        HVMImageHandler.configure(self, vmConfig)
        self.vhpt = int(vmConfig['platform'].get('vhpt',  0))

    def buildDomain(self):
        xc.nvram_init(self.vm.getName(), self.vm.getDomid())
        xc.hvm_set_param(self.vm.getDomid(), HVM_PARAM_VHPT_SIZE, self.vhpt)
        return HVMImageHandler.buildDomain(self)

    def getRequiredAvailableMemory(self, mem_kb):
        page_kb = 16
        # ROM size for guest firmware, io page, xenstore page
        # buffer io page, buffer pio page and memmap info page
        extra_pages = 1024 + 5
        return mem_kb + extra_pages * page_kb

    def getRequiredInitialReservation(self):
        return self.vm.getMemoryTarget()

    def getRequiredShadowMemory(self, shadow_mem_kb, maxmem_kb):
        # Explicit shadow memory is not a concept 
        return 0

class IA64_Linux_ImageHandler(LinuxImageHandler):

    def configure(self, vmConfig):
        LinuxImageHandler.configure(self, vmConfig)
        self.vhpt = int(vmConfig['platform'].get('vhpt',  0))

    def buildDomain(self):
        self.flags = self.vhpt
        return LinuxImageHandler.buildDomain(self)

class X86_HVM_ImageHandler(HVMImageHandler):

    def configure(self, vmConfig):
        HVMImageHandler.configure(self, vmConfig)
        self.pae = int(vmConfig['platform'].get('pae',  0))

    def buildDomain(self):
        xc.hvm_set_param(self.vm.getDomid(), HVM_PARAM_PAE_ENABLED, self.pae)
        return HVMImageHandler.buildDomain(self)

    def getRequiredAvailableMemory(self, mem_kb):
        # Add 8 MiB overhead for QEMU's video RAM.
        return mem_kb + 8192

    def getRequiredInitialReservation(self):
        return self.vm.getMemoryTarget()

    def getRequiredMaximumReservation(self):
        return self.vm.getMemoryMaximum()

    def getRequiredShadowMemory(self, shadow_mem_kb, maxmem_kb):
        # 256 pages (1MB) per vcpu,
        # plus 1 page per MiB of RAM for the P2M map,
        # plus 1 page per MiB of RAM to shadow the resident processes.  
        # This is higher than the minimum that Xen would allocate if no value 
        # were given (but the Xen minimum is for safety, not performance).
        return max(4 * (256 * self.vm.getVCpuCount() + 2 * (maxmem_kb / 1024)),
                   shadow_mem_kb)


class X86_Linux_ImageHandler(LinuxImageHandler):

    def buildDomain(self):
        # set physical mapping limit
        # add an 8MB slack to balance backend allocations.
        mem_kb = self.getRequiredMaximumReservation() + (8 * 1024)
        xc.domain_set_memmap_limit(self.vm.getDomid(), mem_kb)
        return LinuxImageHandler.buildDomain(self)

_handlers = {
    "powerpc": {
        "linux": PPC_LinuxImageHandler,
    },
    "ia64": {
        "linux": IA64_Linux_ImageHandler,
        "hvm": IA64_HVM_ImageHandler,
    },
    "x86": {
        "linux": X86_Linux_ImageHandler,
        "hvm": X86_HVM_ImageHandler,
    },
}

def findImageHandlerClass(image):
    """Find the image handler class for an image config.

    @param image config
    @return ImageHandler subclass or None
    """
    image_type = image.image_type()
    try:
        return _handlers[arch.type][image_type]
    except KeyError:
        raise VmError('unknown image type: ' + image_type)
