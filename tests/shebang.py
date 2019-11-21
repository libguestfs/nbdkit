#!../nbdkit python

disk = bytearray(1024 * 1024)


def open(readonly):
    print("open: readonly=%d" % readonly)
    return 1


def get_size(h):
    global disk
    return len(disk)


def pread(h, buf, offset):
    global disk
    end = offset + len(buf)
    buf[:] = disk[offset:end]
