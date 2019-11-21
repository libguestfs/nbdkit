import nbdkit

disk = bytearray(1024*1024)


API_VERSION = 2


def config_complete():
    print ("set_error = %r" % nbdkit.set_error)


def open(readonly):
    return 1


def get_size(h):
    global disk
    return len(disk)


def can_write(h):
    return True


def can_flush(h):
    return True


def is_rotational(h):
    return False


def can_trim(h):
    return True


def pread(h, buf, offset, flags):
    global disk
    end = offset + len(buf)
    buf[:] = disk[offset:end]


def pwrite(h, buf, offset, flags):
    global disk
    end = offset + len(buf)
    disk[offset:end] = buf


def flush(h, flags):
    pass


def trim(h, count, offset, flags):
    pass


def zero(h, count, offset, flags):
    global disk
    disk[offset:offset+count] = bytearray(count)
