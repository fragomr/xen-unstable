import string
import sys

from xenmgr import sxp
from xenmgr import PrettyPrint
from xenmgr.XendClient import server

from xenmgr.xm.opts import *

opts = Opts(use="""[options]

Create a domain.
""")

opts.opt('help', short='h',
         fn=set_value, default=0,
         use="Print this help.")

opts.opt('quiet', short='q',
         fn=set_true, default=0,
         use="Quiet.")

opts.opt('path', val='PATH',
         fn=set_value, default='.:/etc/xc',
         use="Search path for default scripts.")

opts.opt('defaults', short='f', val='FILE',
         fn=set_value, default='xmdefaults',
         use="Use the given default script.")

opts.opt('config', short='F', val='FILE',
         fn=set_value, default=None,
         use='Domain configuration to use.')

def set_var(opt, k, v):
    opt.set(v)
    for d in string.split(v, ';' ):
        (k, v) = string.split(d, '=')
        opt.opts.setvar(k, v)

opts.opt('define', short='D', val='VAR=VAL',
         fn=set_var, default=None,
         use="""Set variables before loading defaults, e.g. '-D vmid=3;ip=1.2.3.4'
         to set vmid and ip.""")

opts.opt('dryrun', short='n',
         fn=set_true, default=0,
         use="Dry run - print the config but don't create the domain.")

opts.opt('console', short='c',
         fn=set_true, default=0,
         use="Connect to console after domain is created.")

opts.opt('kernel', short='k', val='FILE',
         use="Path to kernel image.")

opts.opt('ramdisk', short='r', val='FILE',
         fn=set_value, default='',
         use="Path to ramdisk.")

opts.opt('builder', short='b', val='FUNCTION',
         fn=set_value, default='linux',
         use="Function to use to build the domain.")

opts.opt('memory', short='m', val='MEMORY',
         fn=set_value, default=128,
         use="Domain memory in MB.")

opts.opt('disk', short='d', val='phy:DEV,VDEV,MODE',
         fn=append_value, default=[],
         use="""Add a disk device to a domain. The physical device is DEV, which
         is exported to the domain as VDEV. The disk is read-only if MODE is r,
         read-write if mode is 'w'.""")

opts.opt('pci', val='BUS,DEV,FUNC',
         fn=append_value, default=[],
         use="""Add a PCI device to a domain.""")

opts.opt('ipaddr', short='i', val="IPADDR",
         fn=append_value, default=[],
         use="Add an IP address to the domain.")

opts.opt('mac', short='M', val="MAC",
         fn=append_value, default=[],
         use="""Add a network interface with the given mac address to the domain.
         More than one interface may be specified. Interfaces with unspecified MAC addresses
         are allocated a random address.""")

opts.opt('nics', val="N",
         fn=set_int, default=1,
         use="Set the number of network interfaces.")

opts.opt('vnet', val='VNET',
         fn=append_value, default=[],
         use="""Define the vnets for the network interfaces.
         More than one vnet may be given, they are used in order.
         """)

opts.opt('root', short='R', val='DEVICE',
         fn=set_value, default='',
         use="""Set the root= parameter on the kernel command line.
         Use a device, e.g. /dev/sda1, or /dev/nfs for NFS root.""")

opts.opt('extra', short='E', val="ARGS",
         fn=set_value, default='',
         use="Set extra arguments to append to the kernel command line.")

opts.opt('ip', short='I', val='IPADDR',
         fn=set_value, default='',
         use="Set the kernel IP interface address.")

opts.opt('gateway', val="IPADDR",
         fn=set_value, default='',
         use="Set kernel IP gateway.")

opts.opt('netmask', val="MASK",
         fn=set_value, default = '',
         use="Set kernel IP netmask.")

opts.opt('hostname', val="NAME",
         fn=set_value, default='',
         use="Set kernel IP hostname.")

opts.opt('interface', val="INTF",
         fn=set_value, default="eth0",
         use="Set the kernel IP interface name.")

opts.opt('dhcp', val="off|dhcp",
         fn=set_value, default='off',
         use="Set kernel dhcp option.")

opts.opt('nfs_server', val="IPADDR",
         fn=set_value, default=None,
         use="Set the address of the NFS server for NFS root.")

opts.opt('nfs_root', val="PATH",
         fn=set_value, default=None,
         use="Set the path of the root NFS directory.")

def strip(pre, s):
    if s.startswith(pre):
        return s[len(pre):]
    else:
        return s

def make_config(opts):
    
    config = ['config',
              ['name', opts.name ],
              ['memory', opts.memory ] ]
    if opts.cpu:
        config.append(['cpu', opts.cpu])
    
    config_image = [ opts.builder ]
    config_image.append([ 'kernel', os.path.abspath(opts.kernel) ])
    if opts.ramdisk:
        config_image.append([ 'ramdisk', os.path.abspath(opts.ramdisk) ])
    if opts.cmdline_ip:
        cmdline_ip = strip('ip=', opts.cmdline_ip)
        config_image.append(['ip', cmdline_ip])
    if opts.root:
        cmdline_root = strip('root=', opts.root)
        config_image.append(['root', opts.root])
    if opts.extra:
        config_image.append(['args', opts.extra])
    config.append(['image', config_image ])
    	
    config_devs = []
    for (uname, dev, mode) in opts.disk:
        config_vbd = ['vbd',
                      ['uname', uname],
                      ['dev', dev ],
                      ['mode', mode ] ]
        config_devs.append(['device', config_vbd])

    for (bus, dev, func) in opts.pci:
        config_pci = ['pci', ['bus', bus], ['dev', dev], ['func', func]]
        config_devs.append(['device', config_pci])

    for idx in range(0, opts.nics):
        config_vif = ['vif', ['@', ['id', 'vif%d' % idx]]]
        if idx < len(opts.mac):
            config_vif.append(['mac', opts.mac[idx]])
        config_devs.append(['device', config_vif])

    config += config_devs

##     if vfr_ipaddr:
##         config_vfr = ['vfr']
##         idx = 0 # No way of saying which IP is for which vif?
##         for ip in vfr_ipaddr:
##             config_vfr.append(['vif', ['id', idx], ['ip', ip]])
##         config.append(config_vfr)

    if opts.vnet:
        config_vnet = ['vnet']
        idx = 0
        for vnet in opts.vnet:
            config_vif = ['vif', ['id', 'vif%d' % idx], ['vnet', vnet]]
            config_vnet.append(config_vif)
            idx += 1
        config.append(config_vnet)
            
    return config

def preprocess_disk(opts):
    if not opts.disk: return
    disk = []
    for v in opts.disk:
        d = v.split(',')
        print 'disk', v, d
        if len(d) != 3:
            opts.err('Invalid disk specifier: ' + v)
        disk.append(d)
    opts.disk = disk

def preprocess_pci(opts):
    if not opts.pci: return
    pci = []
    for v in opts.pci:
        d = v.split(',')
        if len(d) != 3:
            opts.err('Invalid pci specifier: ' + v)
        pci.append(d)
    opts.pci = pci

def preprocess_ip(opts):
    setip = (opts.hostname or opts.netmask
             or opts.gateway or opts.dhcp or opts.interface)
    if not setip: return
    ip = (opts.ip
          + ':'
          + ':' + opts.gateway
          + ':' + opts.netmask
          + ':' + opts.hostname
          + ':' + opts.interface
          + ':' + opts.dhcp)
    opts.cmdline_ip = ip

def preprocess_nfs(opts):
    if (opts.nfs_root or opts.nfs_server):
        if (not opts.nfs_root) or (not opts.nfs_server):
            opts.err('Must set nfs root and nfs server')
    else:
        return
    nfs = 'nfsroot=' + opts.nfs_server + ':' + opts.nfs_root
    opts.extra = nfs + ' ' + opts.extra
    
def preprocess(opts):
    preprocess_disk(opts)
    preprocess_pci(opts)
    preprocess_ip(opts)
    preprocess_nfs(opts)
         
def make_domain(opts, config):
    """Create, build and start a domain.
    Returns: [int] the ID of the new domain.
    """
    restore = 0 #todo

    if restore:
        dominfo = server.xend_domain_restore(state_file, config)
    else:
        dominfo = server.xend_domain_create(config)

    dom = int(sxp.child_value(dominfo, 'id'))
    console_info = sxp.child(dominfo, 'console')
    if console_info:
        console_port = int(sxp.child_value(console_info, 'port'))
    else:
        console_port = None
    
    if server.xend_domain_unpause(dom) < 0:
        server.xend_domain_halt(dom)
        opts.err("Failed to start domain %d" % dom)
    opts.info("Started domain %d, console on port %d"
              % (dom, console_port))
    return (dom, console_port)

def main(argv):
    args = opts.parse(argv)
    if opts.config:
        pass
    else:
        opts.load_defaults()
    if opts.help:
        opts.usage()
    preprocess(opts)
    config = make_config(opts)
    if opts.dryrun:
        PrettyPrint.prettyprint(config)
    else:
        make_domain(opts, config)
        
if __name__ == '__main__':
    main(sys.argv)
