# Example Tcl plugin.
#
# This example can be freely used for any purpose.

# Run it from the build directory like this:
#
#   ./nbdkit -f -v tcl ./plugins/tcl/example.tcl file=disk.img
#
# Or run it after installing nbdkit like this:
#
#   nbdkit -f -v tcl ./plugins/tcl/example.tcl file=disk.img
#
# The -f -v arguments are optional.  They cause the server to stay in
# the foreground and print debugging, which is useful when testing.
#
# You can connect to the server using guestfish or qemu, eg:
#
#   guestfish --format=raw -a nbd://localhost
#   ><fs> run
#   ><fs> part-disk /dev/sda mbr
#   ><fs> mkfs ext2 /dev/sda1
#   ><fs> list-filesystems
#   ><fs> mount /dev/sda1 /
#   ><fs> [etc]

# This is called from: nbdkit tcl example.tcl --dump-plugin
proc dump_plugin {} {
    puts "example_tcl=1"
}

# We expect a file=... parameter pointing to the file to serve.
proc config {key value} {
    global file

    if { $key == "file" } {
        set file $value
    } else {
        error "unknown parameter $key=$value"
    }
}

# Check the file parameter was passed.
proc config_complete {} {
    global file

    if { ![info exists file] } {
        error "file parameter missing"
    }
}

# Open a new client connection.
proc plugin_open {readonly} {
    global file

    # Open the file.
    if { $readonly } {
        set flags "r"
    } else {
        set flags "r+"
    }
    set fh [open $file $flags]

    # Stop Tcl from trying to convert to and from UTF-8.
    fconfigure $fh -translation binary

    # We can return any Tcl object as the handle.  In this
    # plugin it's convenient to return the file handle.
    return $fh
}

# Close a client connection.
proc plugin_close {fh} {
    close $fh
}

proc get_size {fh} {
    global file

    return [file size $file]
}

proc pread {fh count offset} {
    seek $fh $offset
    return [read $fh $count]
}

proc pwrite {fh buf offset} {
    seek $fh $offset
    puts -nonewline $fh $buf
}
