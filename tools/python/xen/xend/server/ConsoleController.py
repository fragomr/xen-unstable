from xen.xend.server.DevController import DevController
from xen.xend.XendLogging import log

from xen.xend.XendError import VmError

class ConsoleController(DevController):
    """A dummy controller for us to represent serial and vnc
    console devices with persistent UUIDs.
    """

    valid_cfg = ['uri', 'uuid', 'protocol']

    def __init__(self, vm):
        DevController.__init__(self, vm)
        self.hotplug = False

    def getDeviceDetails(self, config):
        back = dict([(k, config[k]) for k in self.valid_cfg if k in config])
        return (self.allocateDeviceID(), back, {})


    def getDeviceConfiguration(self, devid):
        result = DevController.getDeviceConfiguration(self, devid)
        devinfo = self.readBackend(devid, *self.valid_cfg)
        config = dict(zip(self.valid_cfg, devinfo))
        config = dict([(key, val) for key, val in config.items()
                       if val != None])
        return config

