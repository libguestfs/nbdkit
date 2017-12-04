#!../nbdkit ruby

include Nbdkit

$disk = "\0" * (1024 * 1024)

def open(readonly)
  h = {}
  return h
end

def get_size(h)
  return $disk.bytesize
end

def pread(h, count, offset)
  return $disk.byteslice(offset, count)
end
