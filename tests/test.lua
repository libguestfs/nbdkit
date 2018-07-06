disk = string.rep ('\0', 1024*1024)

-- Lua strings are indexed from 1, which is crazy.  This is a
-- sane substring function.
function disk_sub (n, len)
   return disk:sub (n+1, n+len)
end

function open (readonly)
   return 1
end

function get_size (h)
   return disk:len()
end

function pread (h, count, offset)
   return disk_sub(offset, count)
end

function pwrite (h, buf, offset)
   -- There's no built-in mutable string type, so this is going
   -- to be very inefficient.
   local count = buf:len()
   local end_len = disk:len() - (offset+count)
   disk = disk_sub(0, offset) .. buf .. disk_sub(offset+count, end_len)
end
