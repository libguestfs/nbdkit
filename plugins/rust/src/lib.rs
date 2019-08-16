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

use std::mem;
use std::os::raw::{c_char, c_int, c_void};

// This struct describes the plugin ABI which your plugin_init()
// function must return.
#[repr(C)]
pub struct Plugin {
    _struct_size: u64,
    _api_version: c_int,
    _thread_model: c_int,

    pub name: *const c_char,
    pub longname: *const c_char,
    pub version: *const c_char,
    pub description: *const c_char,

    pub load: Option<extern fn ()>,
    pub unload: Option<extern fn ()>,

    pub config: Option<extern fn (*const c_char, *const c_char)>,
    pub config_complete: Option<extern fn () -> c_int>,
    pub config_help: *const c_char,

    pub open: extern fn (c_int) -> *mut c_void,
    pub close: Option<extern fn (*mut c_void)>,

    pub get_size: extern fn (*mut c_void) -> i64,

    pub can_write: Option<extern fn (*mut c_void) -> c_int>,
    pub can_flush: Option<extern fn (*mut c_void) -> c_int>,
    pub is_rotational: Option<extern fn (*mut c_void) -> c_int>,
    pub can_trim: Option<extern fn (*mut c_void) -> c_int>,

    // Slots for old v1 API functions.
    _pread_old: Option<extern fn ()>,
    _pwrite_old: Option<extern fn ()>,
    _flush_old: Option<extern fn ()>,
    _trim_old: Option<extern fn ()>,
    _zero_old: Option<extern fn ()>,

    errno_is_preserved: c_int,

    pub dump_plugin: Option<extern fn ()>,

    pub can_zero: Option<extern fn (*mut c_void) -> c_int>,
    pub can_fua: Option<extern fn (*mut c_void) -> c_int>,

    pub pread: extern fn (h: *mut c_void, buf: *mut c_char, count: u32,
                          offset: u64,
                          flags: u32) -> c_int,
    pub pwrite: Option<extern fn (h: *mut c_void, buf: *const c_char,
                                  count: u32, offset: u64,
                                  flags: u32) -> c_int>,
    pub flush: Option<extern fn (h: *mut c_void, flags: u32) -> c_int>,
    pub trim: Option<extern fn (h: *mut c_void,
                                count: u32, offset: u64,
                                flags: u32) -> c_int>,
    pub zero: Option<extern fn (h: *mut c_void,
                                count: u32, offset: u64,
                                flags: u32) -> c_int>,

    pub magic_config_key: *const c_char,

    pub can_multi_conn: Option<extern fn (h: *mut c_void) -> c_int>,

    // Slots for extents functions, which needs more integration.
    _can_extents: Option<extern fn ()>,
    _extents: Option<extern fn ()>,

    pub can_cache: Option<extern fn (h: *mut c_void) -> c_int>,
    pub cache: Option<extern fn (h: *mut c_void,
                                 count: u32, offset: u64,
                                 flags: u32) -> c_int>,
}

pub enum ThreadModel {
    SerializeConnections = 0,
    SerializeAllRequests = 1,
    SerializeRequests = 2,
    Parallel = 3,
}

impl Plugin {
    pub fn new (thread_model: ThreadModel,
                name: *const c_char,
                open: extern fn (c_int) -> *mut c_void,
                get_size: extern fn (*mut c_void) -> i64,
                pread: extern fn (h: *mut c_void, buf: *mut c_char,
                                  count: u32, offset: u64,
                                  flags: u32) -> c_int) -> Plugin {
        Plugin {
            _struct_size: mem::size_of::<Plugin>() as u64,
            _api_version: 2,
            _thread_model: thread_model as c_int,
            name: name,
            longname: std::ptr::null(),
            version: std::ptr::null(),
            description: std::ptr::null(),
            load: None,
            unload: None,
            config: None,
            config_complete: None,
            config_help: std::ptr::null(),
            open: open,
            close: None,
            get_size: get_size,
            can_write: None,
            can_flush: None,
            is_rotational: None,
            can_trim: None,
            _pread_old: None,
            _pwrite_old: None,
            _flush_old: None,
            _trim_old: None,
            _zero_old: None,
            errno_is_preserved: 0,
            dump_plugin: None,
            can_zero: None,
            can_fua: None,
            pread: pread,
            pwrite: None,
            flush: None,
            trim: None,
            zero: None,
            magic_config_key: std::ptr::null(),
            can_multi_conn: None,
            _can_extents: None,
            _extents: None,
            can_cache: None,
            cache: None,
        }
    }
}
