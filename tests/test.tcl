# XXX This actually creates a Unicode (UCS-2) array.  It's also rather
# slow to run.  It's possible we should be using a list instead.
set disk [string repeat "\u0000" [expr 1024*1024]]

proc plugin_open {readonly} {
    return 1
}

proc get_size {h} {
    global disk

    return [string length $disk]
}

proc pread {h count offset} {
    global disk

    set last [expr $offset+$count-1]
    return [string range $disk $offset $last]
}

proc pwrite {h buf offset} {
    global disk

    set count [string length $buf]
    set last [expr $offset+$count-1]
    set disk [string replace $disk $offset $last $buf]
}
