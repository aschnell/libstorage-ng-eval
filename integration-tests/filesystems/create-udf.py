#!/usr/bin/python3

# requirements: disk /dev/sdd with msdos partition table and partition /dev/sdd1


from storage import *
from storageitu import *


set_logger(get_logfile_logger())

environment = Environment(False)

storage = Storage(environment)
storage.probe()

staging = storage.get_staging()

print(staging)

partition = Partition.find_by_name(staging, "/dev/sdd1")
partition.set_id(ID_LINUX)

udf = partition.create_blk_filesystem(FsType_UDF)
udf.set_label("TEST")

mount_point = udf.create_mount_point("/test")

print(staging)

commit(storage)
