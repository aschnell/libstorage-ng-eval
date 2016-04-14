#!/usr/bin/python

# requirements: partition /dev/sdb1 with at least 512MiB space behind /dev/sdb1


from sys import exit
from storage import *
from storageitu import *


set_logger(get_logfile_logger())

environment = Environment(False)

storage = Storage(environment)

staging = storage.get_staging()

print staging

partition = Partition.find(staging, "/dev/sdb1")

partition.set_size(partition.get_size() + 512 * MiB)

print staging

commit(storage)

