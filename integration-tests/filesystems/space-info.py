#!/usr/bin/python

# requirements: filesystem mounted at /test


from sys import exit
from storage import *
from storageitu import *


set_logger(get_logfile_logger())

environment = Environment(False)

storage = Storage(environment)

probed = storage.get_probed()

print probed

filesystems = Filesystem.find_by_mountpoint(probed, "/test")

if not filesystems.empty():

    filesystem = filesystems[0]

    space_info = filesystem.detect_space_info()

    print space_info

    print byte_to_humanstring(space_info.size, False, 2, True)
    print byte_to_humanstring(space_info.used, False, 2, True)


