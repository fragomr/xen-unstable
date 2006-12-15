from xen.xend.server.DevController import DevController
from xen.xend.XendLogging import log

from xen.xend.XendError import VmError
import xen.xend
import os

def spawn_detached(path, args, env):
    p = os.fork()
    if p == 0:
        os.spawnve(os.P_NOWAIT, path, args, env)
        os._exit(0)
    else:
        os.waitpid(p, 0)
        
class VfbifController(DevController):
    """Virtual frame buffer controller. Handles all vfb devices for a domain.
    Note that we only support a single vfb per domain at the moment.
    """

    def __init__(self, vm):
        DevController.__init__(self, vm)
        self.config = {}
        
    def getDeviceDetails(self, config):
        """@see DevController.getDeviceDetails"""
        devid = 0
        back = {}
        front = {}
        return (devid, back, front)

    def getDeviceConfiguration(self, devid):
        r = DevController.getDeviceConfiguration(self, devid)
        for (k,v) in self.config.iteritems():
            r[k] = v
        return r
    
    def createDevice(self, config):
        DevController.createDevice(self, config)
        self.config = config
        std_args = [ "--domid", "%d" % self.vm.getDomid(),
                     "--title", self.vm.getName() ]
        t = config.get("type", None)
        if t == "vnc":
            passwd = None
            if config.has_key("vncpasswd"):
                passwd = config["vncpasswd"]
            else:
                passwd = xen.xend.XendRoot.instance().get_vncpasswd_default()
            if not(passwd is None or passwd == ""):
                self.vm.storeVm("vncpasswd", passwd)
                log.debug("Stored a VNC password for vfb access")
            else:
                log.debug("No VNC passwd configured for vfb access")

            # Try to start the vnc backend
            args = [xen.util.auxbin.pathTo("xen-vncfb")]
            if config.has_key("vncunused"):
                args += ["--unused"]
            elif config.has_key("vncdisplay"):
                args += ["--vncport", "%d" % (5900 + config["vncdisplay"])]
            vnclisten = config.get("vnclisten",
                                   xen.xend.XendRoot.instance().get_vnclisten_address())
            args += [ "--listen", vnclisten ]
            spawn_detached(args[0], args + std_args, os.environ)
        elif t == "sdl":
            args = [xen.util.auxbin.pathTo("xen-sdlfb")]
            env = dict(os.environ)
            if config.has_key("display"):
                env['DISPLAY'] = config["display"]
            if config.has_key("xauthority"):
                env['XAUTHORITY'] = config["xauthority"]
            spawn_detached(args[0], args + std_args, env)
        else:
            raise VmError('Unknown vfb type %s (%s)' % (t, repr(config)))

class VkbdifController(DevController):
    """Virtual keyboard controller. Handles all vkbd devices for a domain.
    """

    def getDeviceDetails(self, config):
        """@see DevController.getDeviceDetails"""
        devid = 0
        back = {}
        front = {}
        return (devid, back, front)
