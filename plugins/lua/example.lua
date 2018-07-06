-- Example Lua plugin.
--
-- This example can be freely used for any purpose.

-- Run it from the build directory like this:
--
--   ./nbdkit -f -v lua ./plugins/lua/example.lua file=disk.img
--
-- Or run it after installing nbdkit like this:
--
--   nbdkit -f -v lua ./plugins/lua/example.lua file=disk.img
--
-- The -f -v arguments are optional.  They cause the server to stay in
-- the foreground and print debugging, which is useful when testing.
--
-- You can connect to the server using guestfish or qemu, eg:
--
--   guestfish --format=raw -a nbd://localhost
--   ><fs> run
--   ><fs> part-disk /dev/sda mbr
--   ><fs> mkfs ext2 /dev/sda1
--   ><fs> list-filesystems
--   ><fs> mount /dev/sda1 /
--   ><fs> [etc]

-- This is called from: nbdkit lua example.lua --dump-plugin
function dump_plugin ()
   print ("example_lua=1")
end

-- We expect a file=... parameter pointing to the file to serve.
function config (key, value)
   if key == "file" then
      file = value
   else
      error ("unknown parameter " .. key .. "=" .. value)
   end
end

-- Check the file parameter was passed.
function config_complete ()
   if not file then
      error ("file parameter missing")
   end
end

-- Open a new client connection.
function open (readonly)
   -- Open the file.
   local flags
   if readonly then
      flags="rb"
   else
      flags="r+b"
   end
   local fh = assert (io.open (file, flags))

    -- We can return any Lua object as the handle.  In this
    -- plugin it's convenient to return the file handle.
   return fh
end

-- Close a client connection.
function close (fh)
   assert (fh:close())
end

function get_size (fh)
   local size = assert (fh:seek ("end"))
   return size
end

function pread (fh, count, offset)
   assert (fh:seek ("set", offset))
   local data = assert (fh:read (count))
   return data
end

function pwrite (fh, buf, offset)
   assert (fh:seek ("set", offset))
   assert (fh:write (buf))
end
