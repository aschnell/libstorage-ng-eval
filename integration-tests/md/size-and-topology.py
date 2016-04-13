#!/usr/bin/python

# requirements: disk /dev/sdb with 8 empty and unused partitions preferably of
# slightly different size (sdb1-sdb8)


from sys import exit
from os import system
from storage import *
from storageitu import *


results = open("size-and-topology.txt", 'w')


set_logger(get_logfile_logger())

environment = Environment(False)


def read_int(filename):
    file = open(filename)
    line = file.readline()
    return int(line)


def doit(level, devices, chunk_size_k):

    print
    print "level:", get_md_level_name(level), "devices:", devices, "chunk-size-k:", chunk_size_k
    print

    storage = Storage(environment)
    staging = storage.get_staging()

    md = Md.create(staging, "/dev/md0")
    md.set_md_level(level)
    md.set_chunk_size_k(chunk_size_k)

    for number in range(1, devices + 1):
        print '/dev/sdb%d' % number
        partition = Partition.find(staging, '/dev/sdb%d' % number)
        md.add_device(partition)

    expected_size = md.get_size_k()
    expected_io_size = md.get_topology().get_optimal_io_size()

    commit(storage)

    sysfs_path = md.get_sysfs_path()
    seen_size = read_int("/sys" + sysfs_path + "/size") / 2
    seen_io_size = read_int("/sys" + sysfs_path + "/queue/optimal_io_size")

    size_ok = expected_size == seen_size
    io_size_ok = expected_io_size == seen_io_size

    results.write("level:%s, devices:%d, chunk-size-k:%d" %(get_md_level_name(level), devices, chunk_size_k))
    if size_ok:
        results.write(", size %d" % (expected_size))
    else:
        results.write(", size %d != %d" % (expected_size, seen_size))
    if io_size_ok:
        results.write(", io-size %d" % (expected_io_size))
    else:
        results.write(", io-size %d != %d" % (expected_io_size, seen_io_size))
    results.write("\n")


def cleanup():
    storage = Storage(environment)
    staging = storage.get_staging()
    md = Md.find(staging, "/dev/md0")
    staging.remove_device(md)
    commit(storage)


max_devices = 8


for devices in range(2, max_devices + 1):
    for chunk_size_k in range(5, 10):
        doit(RAID0, devices, 2**chunk_size_k)
        cleanup()

results.write("\n")

for devices in range(2, max_devices + 1):
    doit(RAID1, devices, 0)
    cleanup()

results.write("\n")

for devices in range(3, max_devices + 1):
    for chunk_size_k in range(5, 10):
        doit(RAID5, devices, 2**chunk_size_k)
        cleanup()

results.write("\n")

for devices in range(4, max_devices + 1):
    for chunk_size_k in range(5, 10):
        doit(RAID6, devices, 2**chunk_size_k)
        cleanup()

results.write("\n")

for devices in range(2, max_devices + 1):
    for chunk_size_k in range(5, 10):
        doit(RAID10, devices, 2**chunk_size_k)
        cleanup()
