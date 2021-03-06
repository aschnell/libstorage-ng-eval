#!/usr/bin/python3

# requirements: partition /dev/sdc1 with reiserfs


from storage import *
from storageitu import *


set_logger(get_logfile_logger())

environment = Environment(False)

storage = Storage(environment)
storage.probe()

staging = storage.get_staging()

print(staging)

sdc1 = BlkDevice.find_by_name(staging, "/dev/sdc1")

reiserfs = sdc1.get_blk_filesystem()

reiserfs.set_tune_options("-m 20")

print(staging)

commit(storage)

