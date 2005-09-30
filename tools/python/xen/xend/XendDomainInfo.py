#===========================================================================
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
# Copyright (C) 2004, 2005 Mike Wray <mike.wray@hp.com>
# Copyright (C) 2005 XenSource Ltd
#============================================================================

"""Representation of a single domain.
Includes support for domain construction, using
open-ended configurations.

Author: Mike Wray <mike.wray@hp.com>

"""

import string
import time
import threading
import errno

import xen.lowlevel.xc
from xen.util.blkif import blkdev_uname_to_file

from xen.xend.server import channel

from xen.xend import image
from xen.xend import scheduler
from xen.xend import sxp
from xen.xend import XendRoot
from xen.xend.XendBootloader import bootloader
from xen.xend.XendLogging import log
from xen.xend.XendError import XendError, VmError
from xen.xend.XendRoot import get_component

from xen.xend.uuid import getUuid
from xen.xend.xenstore.xstransact import xstransact
from xen.xend.xenstore.xsutil import GetDomainPath, IntroduceDomain

"""Shutdown code for poweroff."""
DOMAIN_POWEROFF = 0

"""Shutdown code for reboot."""
DOMAIN_REBOOT   = 1

"""Shutdown code for suspend."""
DOMAIN_SUSPEND  = 2

"""Shutdown code for crash."""
DOMAIN_CRASH    = 3

"""Map shutdown codes to strings."""
shutdown_reasons = {
    DOMAIN_POWEROFF: "poweroff",
    DOMAIN_REBOOT  : "reboot",
    DOMAIN_SUSPEND : "suspend",
    DOMAIN_CRASH   : "crash",
    }

restart_modes = [
    "restart",
    "destroy",
    "preserve",
    "rename-restart"
    ]

STATE_VM_OK         = "ok"
STATE_VM_TERMINATED = "terminated"
STATE_VM_SUSPENDED  = "suspended"

"""Flag for a block device backend domain."""
SIF_BLK_BE_DOMAIN = (1<<4)

"""Flag for a net device backend domain."""
SIF_NET_BE_DOMAIN = (1<<5)

"""Flag for a TPM device backend domain."""
SIF_TPM_BE_DOMAIN = (1<<7)


SHUTDOWN_TIMEOUT = 30


DOMROOT = '/local/domain/'
VMROOT  = '/vm/'


xc = xen.lowlevel.xc.new()
xroot = XendRoot.instance()


## Configuration entries that we expect to round-trip -- be read from the
# config file or xc, written to save-files (i.e. through sxpr), and reused as
# config on restart or restore, all without munging.  Some configuration
# entries are munged for backwards compatibility reasons, or because they
# don't come out of xc in the same form as they are specified in the config
# file, so those are handled separately.
ROUNDTRIPPING_CONFIG_ENTRIES = [
        ('name',         str),
        ('ssidref',      int),
        ('cpu_weight',   float),
        ('bootloader',   str),
        ('on_poweroff',  str),
        ('on_reboot',    str),
        ('on_crash',     str)
    ]


def restore(config):
    """Create a domain and a VM object to do a restore.

    @param config:    domain configuration
    """

    log.debug("XendDomainInfo.restore(%s)", config)

    try:
        uuid    =     sxp.child_value(config, 'uuid')
        ssidref = int(sxp.child_value(config, 'ssidref'))
    except TypeError, exn:
        raise VmError('Invalid ssidref in config: %s' % exn)

    vm = XendDomainInfo(uuid, XendDomainInfo.parseConfig(config),
                        xc.domain_create(ssidref = ssidref))
    vm.storeVmDetails()
    vm.configure()
    vm.create_channel()
    vm.storeDomDetails()
    return vm


def domain_exists(name):
    # See comment in XendDomain constructor.
    xd = get_component('xen.xend.XendDomain')
    return xd.domain_lookup_by_name(name)

def shutdown_reason(code):
    """Get a shutdown reason from a code.

    @param code: shutdown code
    @type  code: int
    @return: shutdown reason
    @rtype:  string
    """
    return shutdown_reasons.get(code, "?")

def dom_get(dom):
    """Get info from xen for an existing domain.

    @param dom: domain id
    @return: info or None
    """
    try:
        domlist = xc.domain_getinfo(dom, 1)
        if domlist and dom == domlist[0]['dom']:
            return domlist[0]
    except Exception, err:
        # ignore missing domain
        log.exception("domain_getinfo(%d) failed, ignoring", dom)
    return None

class XendDomainInfo:
    """Virtual machine object."""

    """Minimum time between domain restarts in seconds.
    """
    MINIMUM_RESTART_TIME = 20


    def create(cls, config):
        """Create a VM from a configuration.

        @param config    configuration
        @raise: VmError for invalid configuration
        """

        log.debug("XendDomainInfo.create(%s)", config)
        
        vm = cls(getUuid(), cls.parseConfig(config))
        vm.construct()
        vm.refreshShutdown()
        return vm

    create = classmethod(create)


    def recreate(cls, xeninfo):
        """Create the VM object for an existing domain.  The domain must not
        be dying, as the paths in the store should already have been removed,
        and asking us to recreate them causes problems."""

        log.debug("XendDomainInfo.recreate(%s)", xeninfo)

        assert not xeninfo['dying']

        domid = xeninfo['dom']
        try:
            dompath = GetDomainPath(domid)
            if not dompath:
                raise XendError(
                    'No domain path in store for existing domain %d' % domid)
            vmpath = xstransact.Read(dompath, "vm")
            if not vmpath:
                raise XendError(
                    'No vm path in store for existing domain %d' % domid)
            uuid = xstransact.Read(vmpath, "uuid")
            if not uuid:
                raise XendError(
                    'No vm/uuid path in store for existing domain %d' % domid)

            log.info("Recreating domain %d, UUID %s.", domid, uuid)

            vm = cls(uuid, xeninfo, domid, True)

        except Exception, exn:
            log.warn(str(exn))

            uuid = getUuid()

            log.info("Recreating domain %d with new UUID %s.", domid, uuid)

            vm = cls(uuid, xeninfo, domid, True)
            vm.storeVmDetails()
            vm.storeDomDetails()

        vm.create_channel()
        if domid == 0:
            vm.initStoreConnection()

        vm.refreshShutdown(xeninfo)
        return vm

    recreate = classmethod(recreate)


    def parseConfig(cls, config):
        def get_cfg(name, conv = None):
            val = sxp.child_value(config, name)

            if conv and not val is None:
                try:
                    return conv(val)
                except TypeError, exn:
                    raise VmError(
                        'Invalid setting %s = %s in configuration: %s' %
                        (name, val, str(exn)))
            else:
                return val


        log.debug("parseConfig: config is %s" % str(config))

        result = {}

        for e in ROUNDTRIPPING_CONFIG_ENTRIES:
            result[e[0]] = get_cfg(e[0], e[1])

        result['memory']       = get_cfg('memory',     int)
        result['mem_kb']       = get_cfg('mem_kb',     int)
        result['maxmem']       = get_cfg('maxmem',     int)
        result['maxmem_kb']    = get_cfg('maxmem_kb',  int)
        result['cpu']          = get_cfg('cpu',        int)
        result['image']        = get_cfg('image')

        try:
            if result['image']:
                result['vcpus'] = int(sxp.child_value(result['image'],
                                                      'vcpus', 1))
            else:
                result['vcpus'] = 1
        except TypeError, exn:
            raise VmError(
                'Invalid configuration setting: vcpus = %s: %s' %
                (sxp.child_value(result['image'], 'vcpus', 1), str(exn)))

        result['backend'] = []
        for c in sxp.children(config, 'backend'):
            result['backend'].append(sxp.name(sxp.child0(c)))

        result['device'] = []
        for d in sxp.children(config, 'device'):
            c = sxp.child0(d)
            result['device'].append((sxp.name(c), c))

        # Configuration option "restart" is deprecated.  Parse it, but
        # let on_xyz override it if they are present.
        restart = get_cfg('restart')
        if restart:
            def handle_restart(event, val):
                if not event in result:
                    result[event] = val

            if restart == "onreboot":
                handle_restart('on_poweroff', 'destroy')
                handle_restart('on_reboot',   'restart')
                handle_restart('on_crash',    'destroy')
            elif restart == "always":
                handle_restart('on_poweroff', 'restart')
                handle_restart('on_reboot',   'restart')
                handle_restart('on_crash',    'restart')
            elif restart == "never":
                handle_restart('on_poweroff', 'destroy')
                handle_restart('on_reboot',   'destroy')
                handle_restart('on_crash',    'destroy')
            else:
                log.warn("Ignoring malformed and deprecated config option "
                         "restart = %s", restart)

        log.debug("parseConfig: result is %s" % str(result))
        return result


    parseConfig = classmethod(parseConfig)

    
    def __init__(self, uuid, info, domid = None, augment = False):

        self.uuid = uuid
        self.info = info

        if domid:
            self.domid = domid
        elif 'dom' in info:
            self.domid = int(info['dom'])
        else:
            self.domid = None

        self.vmpath  = VMROOT + uuid
        if self.domid is None:
            self.dompath = None
        else:
            self.dompath = DOMROOT + str(self.domid)

        if augment:
            self.augmentInfo()

        self.validateInfo()

        self.image = None

        self.store_channel = None
        self.store_mfn = None
        self.console_channel = None
        self.console_mfn = None

        self.state = STATE_VM_OK
        self.state_updated = threading.Condition()
        self.refresh_shutdown_lock = threading.Condition()


    def augmentInfo(self):
        """Augment self.info, as given to us through {@link #recreate}, with
        values taken from the store.  This recovers those values known to xend
        but not to the hypervisor.
        """
        def useIfNeeded(name, val):
            if not self.infoIsSet(name) and val is not None:
                self.info[name] = val

        params = (("name", str),
                  ("on_poweroff",  str),
                  ("on_reboot",    str),
                  ("on_crash",     str),
                  ("image",        str),
                  ("start_time", float))

        from_store = self.gatherVm(*params)

        map(lambda x, y: useIfNeeded(x[0], y), params, from_store)

        device = []
        for c in controllerClasses:
            devconfig = self.getDeviceConfigurations(c)
            if devconfig:
                device.extend(map(lambda x: (c, x), devconfig))

        useIfNeeded('device', device)


    def validateInfo(self):
        """Validate and normalise the info block.  This has either been parsed
        by parseConfig, or received from xc through recreate.
        """
        def defaultInfo(name, val):
            if not self.infoIsSet(name):
                self.info[name] = val()

        try:
            defaultInfo('name',         lambda: "Domain-%d" % self.domid)
            defaultInfo('ssidref',      lambda: 0)
            defaultInfo('on_poweroff',  lambda: "destroy")
            defaultInfo('on_reboot',    lambda: "restart")
            defaultInfo('on_crash',     lambda: "restart")
            defaultInfo('cpu',          lambda: None)
            defaultInfo('cpu_weight',   lambda: 1.0)
            defaultInfo('bootloader',   lambda: None)
            defaultInfo('backend',      lambda: [])
            defaultInfo('device',       lambda: [])
            defaultInfo('image',        lambda: None)

            self.check_name(self.info['name'])

            if isinstance(self.info['image'], str):
                self.info['image'] = sxp.from_string(self.info['image'])

            # Internally, we keep only maxmem_KiB, and not maxmem or maxmem_kb
            # (which come from outside, and are in MiB and KiB respectively).
            # This means that any maxmem or maxmem_kb settings here have come
            # from outside, and maxmem_KiB must be updated to reflect them.
            # If we have both maxmem and maxmem_kb and these are not
            # consistent, then this is an error, as we've no way to tell which
            # one takes precedence.

            # Exactly the same thing applies to memory_KiB, memory, and
            # mem_kb.

            def discard_negatives(name):
                if self.infoIsSet(name) and self.info[name] < 0:
                    del self.info[name]

            def valid_KiB_(mb_name, kb_name):
                discard_negatives(kb_name)
                discard_negatives(mb_name)
                
                if self.infoIsSet(kb_name):
                    if self.infoIsSet(mb_name):
                        mb = self.info[mb_name]
                        kb = self.info[kb_name]
                        if mb * 1024 == kb:
                            return kb
                        else:
                            raise VmError(
                                'Inconsistent %s / %s settings: %s / %s' %
                                (mb_name, kb_name, mb, kb))
                    else:
                        return self.info[kb_name]
                elif self.infoIsSet(mb_name):
                    return self.info[mb_name] * 1024
                else:
                    return None

            def valid_KiB(mb_name, kb_name):
                result = valid_KiB_(mb_name, kb_name)
                if result is None or result < 0:
                    raise VmError('Invalid %s / %s: %s' %
                                  (mb_name, kb_name, result))
                else:
                    return result

            def delIf(name):
                if name in self.info:
                    del self.info[name]

            self.info['memory_KiB'] = valid_KiB('memory', 'mem_kb')
            delIf('memory')
            delIf('mem_kb')
            self.info['maxmem_KiB'] = valid_KiB_('maxmem', 'maxmem_kb')
            delIf('maxmem')
            delIf('maxmem_kb')

            if not self.info['maxmem_KiB']:
                self.info['maxmem_KiB'] = 1 << 30

            if self.info['maxmem_KiB'] > self.info['memory_KiB']:
                self.info['maxmem_KiB'] = self.info['memory_KiB']

            # Validate the given backend names.
            for s in self.info['backend']:
                if s not in backendFlags:
                    raise VmError('Invalid backend type: %s' % s)

            for (n, c) in self.info['device']:
                if not n or not c or n not in controllerClasses:
                    raise VmError('invalid device (%s, %s)' %
                                  (str(n), str(c)))

            for event in ['on_poweroff', 'on_reboot', 'on_crash']:
                if self.info[event] not in restart_modes:
                    raise VmError('invalid restart event: %s = %s' %
                                  (event, str(self.info[event])))

            if 'cpumap' not in self.info:
                if [self.info['vcpus'] == 1]:
                    self.info['cpumap'] = [1];
                else:
                    raise VmError('Cannot create CPU map')

        except KeyError, exn:
            log.exception(exn)
            raise VmError('Unspecified domain detail: %s' % str(exn))


    def readVm(self, *args):
        return xstransact.Read(self.vmpath, *args)

    def writeVm(self, *args):
        return xstransact.Write(self.vmpath, *args)

    def removeVm(self, *args):
        return xstransact.Remove(self.vmpath, *args)

    def gatherVm(self, *args):
        return xstransact.Gather(self.vmpath, *args)

    def storeVm(self, *args):
        return xstransact.Store(self.vmpath, *args)

    def readDom(self, *args):
        return xstransact.Read(self.dompath, *args)

    def writeDom(self, *args):
        return xstransact.Write(self.dompath, *args)

    def removeDom(self, *args):
        return xstransact.Remove(self.dompath, *args)

    def gatherDom(self, *args):
        return xstransact.Gather(self.dompath, *args)

    def storeDom(self, *args):
        return xstransact.Store(self.dompath, *args)


    def storeVmDetails(self):
        to_store = {
            'uuid':               self.uuid,

            # XXX
            'memory/target':      str(self.info['memory_KiB'])
            }

        if self.infoIsSet('image'):
            to_store['image'] = sxp.to_string(self.info['image'])

        for k in ['name', 'ssidref', 'on_poweroff', 'on_reboot', 'on_crash']:
            if self.infoIsSet(k):
                to_store[k] = str(self.info[k])

        log.debug("Storing VM details: %s" % str(to_store))

        self.writeVm(to_store)


    def storeDomDetails(self):
        to_store = {
            'domid':              str(self.domid),
            'vm':                 self.vmpath,

            'memory/target':      str(self.info['memory_KiB'])
            }

        for (k, v) in self.info.items():
            if v:
                to_store[k] = str(v)

        log.debug("Storing domain details: %s" % str(to_store))

        self.writeDom(to_store)


    def setDomid(self, domid):
        """Set the domain id.

        @param dom: domain id
        """
        self.domid = domid
        self.storeDom("domid", self.domid)

    def getDomid(self):
        return self.domid

    def setName(self, name):
        self.check_name(name)
        self.info['name'] = name
        self.storeVm("name", name)

    def getName(self):
        return self.info['name']

    def getDomainPath(self):
        return self.dompath

    def getUuid(self):
        return self.uuid

    def getVCpuCount(self):
        return self.info['vcpus']

    def getSsidref(self):
        return self.info['ssidref']

    def getMemoryTarget(self):
        """Get this domain's target memory size, in KiB."""
        return self.info['memory_KiB']

    def setStoreRef(self, ref):
        self.store_mfn = ref
        self.storeDom("store/ring-ref", ref)


    def getBackendFlags(self):
        return reduce(lambda x, y: x | backendFlags[y],
                      self.info['backend'], 0)


    def refreshShutdown(self, xeninfo = None):
        # If set at the end of this method, a restart is required, with the
        # given reason.  This restart has to be done out of the scope of
        # refresh_shutdown_lock.
        restart_reason = None
        
        self.refresh_shutdown_lock.acquire()
        try:
            if xeninfo is None:
                xeninfo = dom_get(self.domid)
                if xeninfo is None:
                    # The domain no longer exists.  This will occur if we have
                    # scheduled a timer to check for shutdown timeouts and the
                    # shutdown succeeded.  It will also occur if someone
                    # destroys a domain beneath us.  We clean up, just in
                    # case.
                    self.cleanupDomain()
                    self.cleanupVm()
                    return

            if xeninfo['dying']:
                # Dying means that a domain has been destroyed, but has not
                # yet been cleaned up by Xen.  This could persist indefinitely
                # if, for example, another domain has some of its pages
                # mapped.  We might like to diagnose this problem in the
                # future, but for now all we do is make sure that it's not
                # us holding the pages, by calling the cleanup methods.
                self.cleanupDomain()
                self.cleanupVm()
                return

            elif xeninfo['crashed']:
                log.warn('Domain has crashed: name=%s id=%d.',
                         self.info['name'], self.domid)

                if xroot.get_enable_dump():
                    self.dumpCore()

                restart_reason = 'crash'

            elif xeninfo['shutdown']:
                if self.readDom('xend/shutdown'):
                    # We've seen this shutdown already, but we are preserving
                    # the domain for debugging.  Leave it alone.
                    pass
                else:
                    reason = shutdown_reason(xeninfo['shutdown_reason'])

                    log.info('Domain has shutdown: name=%s id=%d reason=%s.',
                             self.info['name'], self.domid, reason)

                    self.clearRestart()

                    if reason == 'suspend':
                        self.state_set(STATE_VM_SUSPENDED)
                        # Don't destroy the domain.  XendCheckpoint will do
                        # this once it has finished.
                    elif reason in ['poweroff', 'reboot']:
                        restart_reason = reason
                    else:
                        self.destroy()

            else:
                # Domain is alive.  If we are shutting it down, then check
                # the timeout on that, and destroy it if necessary.

                sst = self.readDom('xend/shutdown_start_time')
                if sst:
                    sst = float(sst)
                    timeout = SHUTDOWN_TIMEOUT - time.time() + sst
                    if timeout < 0:
                        log.info(
                            "Domain shutdown timeout expired: name=%s id=%s",
                            self.info['name'], self.domid)
                        self.destroy()
                    else:
                        log.debug(
                            "Scheduling refreshShutdown on domain %d in %ds.",
                            self.domid, timeout)
                        scheduler.later(timeout, self.refreshShutdown)
        finally:
            self.refresh_shutdown_lock.release()

        if restart_reason:
            self.maybeRestart(restart_reason)


    def shutdown(self, reason):
        if not reason in shutdown_reasons.values():
            raise XendError('invalid reason:' + reason)
        self.storeDom("control/shutdown", reason)
        if not reason == 'suspend':
            self.storeDom('xend/shutdown_start_time', time.time())


    ## private:

    def clearRestart(self):
        self.removeDom("xend/shutdown_start_time")


    def maybeRestart(self, reason):
        # Dispatch to the correct method based upon the configured on_{reason}
        # behaviour.
        {"destroy"        : self.destroy,
         "restart"        : self.restart,
         "preserve"       : self.preserve,
         "rename-restart" : self.renameRestart}[self.info['on_' + reason]]()


    def preserve(self):
        log.info("Preserving dead domain %s (%d).", self.info['name'],
                 self.domid)


    def renameRestart(self):
        self.restart(True)


    def dumpCore(self):
        """Create a core dump for this domain.  Nothrow guarantee."""
        
        try:
            corefile = "/var/xen/dump/%s.%s.core" % (self.info['name'],
                                                     self.domid)
            xc.domain_dumpcore(dom = self.domid, corefile = corefile)

        except Exception, exn:
            log.error("XendDomainInfo.dumpCore failed: id = %s name = %s: %s",
                      self.domid, self.info['name'], str(exn))


    def closeChannel(self, channel, entry):
        """Close the given channel, if set, and remove the given entry in the
        store.  Nothrow guarantee."""
        
        if channel:
            channel.close()
        try:
            self.removeDom(entry)
        except Exception, exn:
            log.exception(exn)
        

    def closeStoreChannel(self):
        """Close the store channel, if any.  Nothrow guarantee."""

        self.closeChannel(self.store_channel, "store/port")
        self.store_channel = None


    def closeConsoleChannel(self):
        """Close the console channel, if any.  Nothrow guarantee."""

        self.closeChannel(self.console_channel, "console/port")
        self.console_channel = None


    ## public:

    def setConsoleRef(self, ref):
        self.console_mfn = ref
        self.storeDom("console/ring-ref", ref)


    def setMemoryTarget(self, target):
        """Set the memory target of this domain.
        @param target In KiB.
        """
        self.info['memory_KiB'] = target
        self.storeDom("memory/target", target)


    def update(self, info = None):
        """Update with info from xc.domain_getinfo().
        """

        log.debug("XendDomainInfo.update(%s) on domain %d", info, self.domid)

        if not info:
            info = dom_get(self.domid)
            if not info:
                return
            
        self.info.update(info)
        self.validateInfo()
        self.refreshShutdown(info)

        log.debug("XendDomainInfo.update done on domain %d: %s", self.domid,
                  self.info)


    ## private:

    def state_set(self, state):
        self.state_updated.acquire()
        if self.state != state:
            self.state = state
            self.state_updated.notifyAll()
        self.state_updated.release()


    ## public:

    def state_wait(self, state):
        self.state_updated.acquire()
        while self.state != state:
            self.state_updated.wait()
        self.state_updated.release()


    def __str__(self):
        s = "<domain"
        s += " id=" + str(self.domid)
        s += " name=" + self.info['name']
        s += " memory=" + str(self.info['memory_KiB'] / 1024)
        s += " ssidref=" + str(self.info['ssidref'])
        s += ">"
        return s

    __repr__ = __str__


    def createDevice(self, deviceClass, devconfig):
        return self.getDeviceController(deviceClass).createDevice(devconfig)


    def configureDevice(self, deviceClass, devid, devconfig):
        return self.getDeviceController(deviceClass).configureDevice(
            devid, devconfig)


    def destroyDevice(self, deviceClass, devid):
        return self.getDeviceController(deviceClass).destroyDevice(devid)


    def getDeviceSxprs(self, deviceClass):
        return self.getDeviceController(deviceClass).sxprs()


    ## private:

    def getDeviceConfigurations(self, deviceClass):
        return self.getDeviceController(deviceClass).configurations()


    def getDeviceController(self, name):
        if name not in controllerClasses:
            raise XendError("unknown device type: " + str(name))

        return controllerClasses[name](self)


    ## public:

    def sxpr(self):
        sxpr = ['domain',
                ['domid',   self.domid],
                ['uuid',    self.uuid],
                ['memory',  self.info['memory_KiB'] / 1024]]

        for e in ROUNDTRIPPING_CONFIG_ENTRIES:
            if self.infoIsSet(e[0]):
                sxpr.append([e[0], self.info[e[0]]])
        
        sxpr.append(['maxmem', self.info['maxmem_KiB'] / 1024])

        if self.infoIsSet('image'):
            sxpr.append(['image', self.info['image']])

        if self.infoIsSet('device'):
            for (_, c) in self.info['device']:
                sxpr.append(['device', c])

        def stateChar(name):
            if name in self.info:
                if self.info[name]:
                    return name[0]
                else:
                    return '-'
            else:
                return '?'

        state = reduce(
            lambda x, y: x + y,
            map(stateChar,
                ['running', 'blocked', 'paused', 'shutdown', 'crashed',
                 'dying']))

        sxpr.append(['state', state])
        if self.infoIsSet('shutdown'):
            reason = shutdown_reason(self.info['shutdown_reason'])
            sxpr.append(['shutdown_reason', reason])
        if self.infoIsSet('cpu_time'):
            sxpr.append(['cpu_time', self.info['cpu_time']/1e9])
        sxpr.append(['vcpus', self.info['vcpus']])
        sxpr.append(['cpumap', self.info['cpumap']])
        if self.infoIsSet('vcpu_to_cpu'):
            sxpr.append(['cpu', self.info['vcpu_to_cpu'][0]])
            sxpr.append(['vcpu_to_cpu', self.prettyVCpuMap()])
            
        if self.infoIsSet('start_time'):
            up_time =  time.time() - self.info['start_time']
            sxpr.append(['up_time', str(up_time) ])
            sxpr.append(['start_time', str(self.info['start_time']) ])

        if self.store_channel:
            sxpr.append(self.store_channel.sxpr())
        if self.store_mfn:
            sxpr.append(['store_mfn', self.store_mfn])
        if self.console_channel:
            sxpr.append(['console_channel', self.console_channel.sxpr()])
        if self.console_mfn:
            sxpr.append(['console_mfn', self.console_mfn])

        return sxpr


    ## private:

    def prettyVCpuMap(self):
        return '|'.join(map(str,
                            self.info['vcpu_to_cpu'][0:self.info['vcpus']]))


    def check_name(self, name):
        """Check if a vm name is valid. Valid names contain alphabetic characters,
        digits, or characters in '_-.:/+'.
        The same name cannot be used for more than one vm at the same time.

        @param name: name
        @raise: VmError if invalid
        """
        if name is None or name == '':
            raise VmError('missing vm name')
        for c in name:
            if c in string.digits: continue
            if c in '_-.:/+': continue
            if c in string.ascii_letters: continue
            raise VmError('invalid vm name')
        dominfo = domain_exists(name)
        # When creating or rebooting, a domain with my name should not exist.
        # When restoring, a domain with my name will exist, but it should have
        # my domain id.
        if not dominfo:
            return
        if dominfo.is_terminated():
            return
        if self.domid is None:
            raise VmError("VM name '%s' already in use by domain %d" %
                          (name, dominfo.domid))
        if dominfo.domid != self.domid:
            raise VmError("VM name '%s' is used in both domains %d and %d" %
                          (name, self.domid, dominfo.domid))


    def construct(self):
        """Construct the domain.

        @raise: VmError on error
        """

        log.debug('XendDomainInfo.construct: %s %s',
                  str(self.domid),
                  str(self.info['ssidref']))

        self.domid = xc.domain_create(dom = 0, ssidref = self.info['ssidref'])

        if self.domid <= 0:
            raise VmError('Creating domain failed: name=%s' %
                          self.info['name'])

        try:
            self.dompath = DOMROOT + str(self.domid)

            # Ensure that the domain entry is clean.  This prevents a stale
            # shutdown_start_time from killing the domain, for example.
            self.removeDom()

            self.initDomain()
            self.construct_image()
            self.configure()
            self.storeVmDetails()
            self.storeDomDetails()
        except Exception:
            log.exception('Domain construction failed')
            self.destroy()
            raise VmError('Creating domain failed: name=%s' %
                          self.info['name'])


    def initDomain(self):
        log.debug('XendDomainInfo.initDomain: %s %s %s',
                  str(self.domid),
                  str(self.info['memory_KiB']),
                  str(self.info['cpu_weight']))

        if not self.infoIsSet('image'):
            raise VmError('Missing image in configuration')

        self.image = image.create(self,
                                  self.info['image'],
                                  self.info['device'])

        if self.info['bootloader']:
            self.image.handleBootloading()

        xc.domain_setcpuweight(self.domid, self.info['cpu_weight'])
        # XXX Merge with configure_maxmem?
        m = self.image.getDomainMemory(self.info['memory_KiB'])
        xc.domain_setmaxmem(self.domid, m)
        xc.domain_memory_increase_reservation(self.domid, m, 0, 0)

        cpu = self.info['cpu']
        if cpu is not None and cpu != -1:
            xc.domain_pincpu(self.domid, 0, 1 << cpu)

        self.info['start_time'] = time.time()

        log.debug('init_domain> Created domain=%d name=%s memory=%d',
                  self.domid, self.info['name'], self.info['memory_KiB'])


    def configure_vcpus(self):
        d = {}
        for v in range(0, self.info['vcpus']):
            d["cpu/%d/availability" % v] = "online"
        self.writeVm(d)


    def construct_image(self):
        """Construct the boot image for the domain.
        """
        self.create_channel()
        self.image.createImage()
        IntroduceDomain(self.domid, self.store_mfn,
                        self.store_channel.port1, self.dompath)
        self.configure_vcpus()


    ## public:

    def cleanupDomain(self):
        """Cleanup domain resources; release devices.  Idempotent.  Nothrow
        guarantee."""

        self.state_set(STATE_VM_TERMINATED)
        self.release_devices()
        self.closeStoreChannel()
        self.closeConsoleChannel()

        if self.image:
            try:
                self.image.destroy()
            except:
                log.exception(
                    "XendDomainInfo.cleanup: image.destroy() failed.")
            self.image = None

        try:
            self.removeDom()
        except Exception:
            log.exception("Removing domain path failed.")


    def cleanupVm(self):
        """Cleanup VM resources.  Idempotent.  Nothrow guarantee."""

        try:
            self.removeVm()
        except Exception:
            log.exception("Removing VM path failed.")


    def destroy(self):
        """Cleanup VM and destroy domain.  Nothrow guarantee."""

        log.debug("XendDomainInfo.destroy: domid=%s", str(self.domid))

        self.cleanupDomain()
        self.cleanupVm()
        
        try:
            if self.domid is not None:
                xc.domain_destroy(dom=self.domid)
        except Exception:
            log.exception("XendDomainInfo.destroy: xc.domain_destroy failed.")


    ## private:

    def is_terminated(self):
        """Check if a domain has been terminated.
        """
        return self.state == STATE_VM_TERMINATED


    def release_devices(self):
        """Release all domain's devices.  Nothrow guarantee."""

        while True:
            t = xstransact("%s/device" % self.dompath)
            for n in controllerClasses.keys():
                for d in t.list(n):
                    try:
                        t.remove(d)
                    except ex:
                        # Log and swallow any exceptions in removal --
                        # there's nothing more we can do.
                        log.exception(
                           "Device release failed: %s; %s; %s",
                           self.info['name'], n, d)
            if t.commit():
                break


    def eventChannel(self, path=None):
        """Create an event channel to the domain.
        
        @param path under which port is stored in db
        """
        port = 0
        if path:
            try:
                port = int(self.readDom(path))
            except:
                # The port is not yet set, i.e. the channel has not yet been
                # created.
                pass

        # Stale port information from above causes an Invalid Argument to be
        # thrown by the eventChannel call below.  To recover, we throw away
        # port if it turns out to be bad, and just create a new channel.
        # If creating a new channel with two new ports fails, then something
        # else is going wrong, so we bail.
        while True:
            try:
                ret = channel.eventChannel(0, self.domid, port1 = port,
                                           port2 = 0)
                break
            except:
                log.exception("Exception in eventChannel(0, %d, %d, %d)",
                              self.domid, port, 0)
                if port == 0:
                    raise
                else:
                    port = 0
                    log.error("Recovering from above exception.")
        self.storeDom(path, ret.port1)
        return ret

    def create_channel(self):
        """Create the channels to the domain.
        """
        self.store_channel = self.eventChannel("store/port")
        self.console_channel = self.eventChannel("console/port")

    def create_configured_devices(self):
        for (n, c) in self.info['device']:
            self.createDevice(n, c)


    def create_devices(self):
        """Create the devices for a vm.

        @raise: VmError for invalid devices
        """
        self.create_configured_devices()
        if self.image:
            self.image.createDeviceModel()


    ## public:

    def device_create(self, dev_config):
        """Create a new device.

        @param dev_config: device configuration
        """
        dev_type = sxp.name(dev_config)
        devid = self.createDevice(dev_type, dev_config)
#        self.config.append(['device', dev.getConfig()])
        return self.getDeviceController(dev_type).sxpr(devid)


    def device_configure(self, dev_config, devid):
        """Configure an existing device.
        @param dev_config: device configuration
        @param devid:      device id
        """
        deviceClass = sxp.name(dev_config)
        self.configureDevice(deviceClass, devid, dev_config)


    ## private:

    def restart_check(self):
        """Check if domain restart is OK.
        To prevent restart loops, raise an error if it is
        less than MINIMUM_RESTART_TIME seconds since the last restart.
        """
        tnow = time.time()
        if self.restart_time is not None:
            tdelta = tnow - self.restart_time
            if tdelta < self.MINIMUM_RESTART_TIME:
                self.restart_cancel()
                msg = 'VM %s restarting too fast' % self.info['name']
                log.error(msg)
                raise VmError(msg)
        self.restart_time = tnow
        self.restart_count += 1


    def restart(self, rename = False):
        """Restart the domain after it has exited.

        @param rename True if the old domain is to be renamed and preserved,
        False if it is to be destroyed.
        """

        #            self.restart_check()

        config = self.sxpr()

        if self.readVm('xend/restart_in_progress'):
            log.error('Xend failed during restart of domain %d.  '
                      'Refusing to restart to avoid loops.',
                      self.domid)
            self.destroy()
            return

        self.writeVm('xend/restart_in_progress', 'True')

        try:
            if rename:
                self.preserveShutdownDomain()
            else:
                self.cleanupDomain()
                self.destroy()
                
            try:
                xd = get_component('xen.xend.XendDomain')
                xd.domain_unpause(xd.domain_create(config).getDomid())
            except Exception, exn:
                log.exception('Failed to restart domain %d.', self.domid)
        finally:
            self.removeVm('xend/restart_in_progress')
            
        # self.configure_bootloader()
        #        self.exportToDB()


    def preserveShutdownDomain(self):
        """Preserve a domain that has been shut down, by giving it a new UUID,
        cloning the VM details, and giving it a new name.  This allows us to
        keep this domain for debugging, but restart a new one in its place
        preserving the restart semantics (name and UUID preserved).
        """
        
        new_name = self.generateShutdownName()
        new_uuid = getUuid()
        log.info("Renaming dead domain %s (%d, %s) to %s (%s).",
                 self.info['name'], self.domid, self.uuid, new_name, new_uuid)
        self.release_devices()
        self.info['name'] = new_name
        self.uuid = new_uuid
        self.vmpath = VMROOT + new_uuid
        self.storeVmDetails()
        self.storeDom('vm', self.vmpath)
        self.storeDom('xend/shutdown', 'True')


    def generateShutdownName(self):
        n = 1
        while True:
            name = "%s-%d" % (self.info['name'], n)
            try:
                self.check_name(name)
                return name
            except VmError:
                n += 1


    def configure_bootloader(self):
        if not self.info['bootloader']:
            return
        # if we're restarting with a bootloader, we need to run it
        # FIXME: this assumes the disk is the first device and
        # that we're booting from the first disk
        blcfg = None
        # FIXME: this assumes that we want to use the first disk
        dev = sxp.child_value(self.config, "device")
        if dev:
            disk = sxp.child_value(dev, "uname")
            fn = blkdev_uname_to_file(disk)
            blcfg = bootloader(self.info['bootloader'], fn, 1,
                               self.info['vcpus'])
        if blcfg is None:
            msg = "Had a bootloader specified, but can't find disk"
            log.error(msg)
            raise VmError(msg)
        self.config = sxp.merge(['vm', ['image', blcfg]], self.config)


    def configure(self):
        """Configure a vm.

        """
        self.configure_maxmem()
        self.create_devices()


    def configure_maxmem(self):
        if self.image:
            m = self.image.getDomainMemory(self.info['memory_KiB'])
            xc.domain_setmaxmem(self.domid, maxmem_kb = m)


    def vcpu_hotplug(self, vcpu, state):
        """Disable or enable VCPU in domain.
        """
        if vcpu > self.info['vcpus']:
            log.error("Invalid VCPU %d" % vcpu)
            return
        if int(state) == 0:
            availability = "offline"
        else:
            availability = "online"
        self.storeVm("cpu/%d/availability" % vcpu, availability)

    def send_sysrq(self, key=0):
        self.storeDom("control/sysrq", '%c' % key)


    def initStoreConnection(self):
        ref = xc.init_store(self.store_channel.port2)
        if ref and ref >= 0:
            self.setStoreRef(ref)
            try:
                IntroduceDomain(self.domid, ref, self.store_channel.port1,
                                self.dompath)
            except RuntimeError, ex:
                if ex.args[0] == errno.EISCONN:
                    pass
                else:
                    raise
        self.configure_vcpus()


    def dom0_enforce_vcpus(self):
        dom = 0
        # get max number of vcpus to use for dom0 from config
        target = int(xroot.get_dom0_vcpus())
        log.debug("number of vcpus to use is %d" % (target))
   
        # target = 0 means use all processors
        if target > 0:
            # count the number of online vcpus (cpu values in v2c map >= 0)
            vcpu_to_cpu = dom_get(dom)['vcpu_to_cpu']
            vcpus_online = len(filter(lambda x: x >= 0, vcpu_to_cpu))
            log.debug("found %d vcpus online" % (vcpus_online))

            # disable any extra vcpus that are online over the requested target
            for vcpu in range(target, vcpus_online):
                log.info("enforcement is disabling DOM%d VCPU%d" % (dom, vcpu))
                self.vcpu_hotplug(vcpu, 0)


    def infoIsSet(self, name):
        return name in self.info and self.info[name] is not None


#============================================================================
# Register device controllers and their device config types.

"""A map from device-class names to the subclass of DevController that
implements the device control specific to that device-class."""
controllerClasses = {}


"""A map of backend names and the corresponding flag."""
backendFlags = {}


def addControllerClass(device_class, backend_name, backend_flag, cls):
    """Register a subclass of DevController to handle the named device-class.

    @param backend_flag One of the SIF_XYZ_BE_DOMAIN constants, or None if
    no flag is to be set.
    """
    cls.deviceClass = device_class
    backendFlags[backend_name] = backend_flag
    controllerClasses[device_class] = cls


from xen.xend.server import blkif, netif, tpmif, pciif, usbif
addControllerClass('vbd',  'blkif', SIF_BLK_BE_DOMAIN, blkif.BlkifController)
addControllerClass('vif',  'netif', SIF_NET_BE_DOMAIN, netif.NetifController)
addControllerClass('vtpm', 'tpmif', SIF_TPM_BE_DOMAIN, tpmif.TPMifController)
addControllerClass('pci',  'pciif', None,              pciif.PciController)
addControllerClass('usb',  'usbif', None,              usbif.UsbifController)
