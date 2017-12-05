#!../nbdkit python

disk = bytearray(1024 * 1024)

def open(readonly):
    print ("open: readonly=%d" % readonly)
    return 1

def get_size(h):
    global disk
    return len (disk)

def pread(h, count, offset):
    global disk
    return disk[offset:offset+count]
