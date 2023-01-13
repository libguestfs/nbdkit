// vim: tw=80
// Copyright (C) 2020 Axcient
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

//! A bare bones plugin that only defines the minimum callbacks
// Test cases for different plugins must be run in separate processes due to the
// one-time initialized global state.

use nbdkit::*;
use std::os::raw::c_char;
use std::sync::Once;

mod common;
use common::*;

static mut PLUGIN: Option<*const Plugin> = None;
static INIT: Once = Once::new();

plugin!(MockServer{});

// One-time initialization of the plugin
pub fn initialize() {
    INIT.call_once(|| {
        let _m = MOCK_SERVER_MTX.lock().unwrap();
        let config_help_ctx = MockServer::config_help_context();
        config_help_ctx.expect()
            .return_const(None);
        let desc_ctx = MockServer::description_context();
        desc_ctx.expect()
            .return_const(None);
        let longname_ctx = MockServer::longname_context();
        longname_ctx.expect()
            .return_const(None);
        let magic_config_key_ctx = MockServer::magic_config_key_context();
        magic_config_key_ctx.expect()
            .return_const(Some("magic_config_key"));
        let name_ctx = MockServer::name_context();
        name_ctx.expect()
            .return_const("Mock NBD Server");
        let version_ctx = MockServer::version_context();
        version_ctx.expect()
            .return_const(None);
        unsafe {
            PLUGIN = Some(plugin_init());
        }
    });
}

fn with_fixture<F: FnMut(&mut Fixture)>(mut f: F) {
    initialize();
    let _m = MOCK_SERVER_MTX.lock().unwrap();

    let mut mock = Box::<MockServer>::default();
    mock.expect_get_size()
        .returning(|| Ok(0x4200));
    let mockp = (&mut mock) as *mut Box<MockServer>;
    let open_ctx = MockServer::open_context();
    open_ctx.expect()
        .return_once(|_| mock);

    let pluginp = unsafe { PLUGIN.unwrap()};
    let plugin = unsafe {&*pluginp};
    let handle = (plugin.open)(0);
    let mut fixture = Fixture {
        mockp,
        plugin,
        handle
    };

    f(&mut fixture);

    (plugin.close)(handle);
}


#[test]
fn open_close() {
    with_fixture(|fixture| {
        let size = (fixture.plugin.get_size)(fixture.handle);
        assert_eq!(0x4200, size);
    });
}

mod pread {
    use super::*;

    #[test]
    fn eio() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_read_at()
                .returning(|_, _| Err(Error::new(libc::EIO, "Mock error")));
            let mut buf = vec![0u8; 512];
            assert_eq!(-1, (fixture.plugin.pread)(fixture.handle,
                                                  buf.as_mut_ptr() as *mut c_char,
                                                  buf.len() as u32,
                                                  1024,
                                                  0));
            ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
            ERRMSG.with(|e| assert_eq!("Mock error", *e.borrow()));

        });
    }

    #[test]
    fn ok() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_read_at()
            .withf(|buf, offset|
                   buf.len() == 512 &&
                   *offset == 1024
            ).returning(|buf, _offset| {
                buf[0..10].copy_from_slice(b"0123456789");
                Ok(())
            });
            let mut buf = vec![0u8; 512];
            assert_eq!(0, (fixture.plugin.pread)(fixture.handle,
                                                 buf.as_mut_ptr() as *mut c_char,
                                                 buf.len() as u32,
                                                 1024,
                                                 0));
            assert_eq!(&buf[0..10], b"0123456789");
        });
    }
}
