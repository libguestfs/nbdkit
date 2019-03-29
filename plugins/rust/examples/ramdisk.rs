// nbdkit
// Copyright (C) 2019 Red Hat Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// * Neither the name of Red Hat nor the names of its contributors may be
// used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
// USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
// OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

extern crate nbdkit;

#[macro_use]
extern crate lazy_static;

use libc::*;
use std::ptr;
use std::os::raw::c_int;
use std::sync::Mutex;

use nbdkit::*;
use nbdkit::ThreadModel::*;

// The RAM disk.
lazy_static! {
    static ref DISK: Mutex<Vec<u8>> = Mutex::new (vec![0; 100 * 1024 * 1024]);
}

struct Handle {
    // Box::new doesn't allocate anything unless we put some dummy
    // fields here.  In a real implementation you would put per-handle
    // data here as required.
    _not_used: i32,
}

extern fn ramdisk_open (_readonly: c_int) -> *mut c_void {
    let h = Handle {_not_used: 0};
    let h = Box::new(h);
    return Box::into_raw(h) as *mut c_void;
}

extern fn ramdisk_close (h: *mut c_void) {
    let h = unsafe { Box::from_raw(h as *mut Handle) };
    drop (h);
}

extern fn ramdisk_get_size (_h: *mut c_void) -> int64_t {
    return DISK.lock().unwrap().capacity() as int64_t;
}

extern fn ramdisk_pread (_h: *mut c_void, buf: *mut c_char, count: uint32_t,
                         offset: uint64_t, _flags: uint32_t) -> c_int {
    let offset = offset as usize;
    let count = count as usize;
    let disk = DISK.lock().unwrap();
    unsafe {
        ptr::copy_nonoverlapping (&disk[offset], buf as *mut u8, count);
    }
    return 0;
}

extern fn ramdisk_pwrite (_h: *mut c_void, buf: *const c_char, count: uint32_t,
                          offset: uint64_t, _flags: uint32_t) -> c_int {
    let offset = offset as usize;
    let count = count as usize;
    let mut disk = DISK.lock().unwrap();
    unsafe {
        ptr::copy_nonoverlapping (buf as *const u8, &mut disk[offset], count);
    }
    return 0;
}

// Every plugin must define a public, C-compatible plugin_init
// function which returns a pointer to a Plugin struct.
#[no_mangle]
pub extern fn plugin_init () -> *const Plugin {
    // Plugin name.
    // https://github.com/rust-lang/rfcs/issues/400
    let name = "ramdisk\0" as *const str as *const [c_char] as *const c_char;

    // Create a mutable plugin, setting the 5 required fields.
    let mut plugin = Plugin::new (
        Parallel,
        name,
        ramdisk_open,
        ramdisk_get_size,
        ramdisk_pread
    );
    // Update any other fields as required.
    plugin.close = Some (ramdisk_close);
    plugin.pwrite = Some (ramdisk_pwrite);

    // Return the pointer.
    let plugin = Box::new(plugin);
    // XXX Memory leak.
    return Box::into_raw(plugin);
}
