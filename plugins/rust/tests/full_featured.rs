//vim: tw=80
//! A full-featured plugin that implements all optional callbacks
// Test cases for different plugins must be run in separate processes due to the
// one-time initialized global state.

use errno::{Errno, set_errno};
use mockall::predicate::*;
use nbdkit::*;
use std::{
    ffi::CStr,
    sync::Once,
    os::raw::c_char,
    ptr
};

mod common;
use common::*;

static mut PLUGIN: Option<*const Plugin> = None;
static INIT: Once = Once::new();

plugin!(MockServer{
    cache,
    can_cache,
    can_extents,
    can_fast_zero,
    can_flush,
    can_fua,
    can_multi_conn,
    can_trim,
    can_write,
    can_zero,
    config,
    config_complete,
    config_help,
    dump_plugin,
    extents,
    flush,
    get_ready,
    is_rotational,
    load,
    preconnect,
    thread_model,
    trim,
    unload,
    write_at,
    zero
});

// One-time initialization of the plugin
pub fn initialize() {
    INIT.call_once(|| {
        let _m = MOCK_SERVER_MTX.lock().unwrap();
        let config_help_ctx = MockServer::config_help_context();
        config_help_ctx.expect()
            .return_const(Some("Mock config help"));
        let desc_ctx = MockServer::description_context();
        desc_ctx.expect()
            .return_const(Some("Mock description"));
        let longname_ctx = MockServer::longname_context();
        longname_ctx.expect()
            .return_const(Some("Mock long name"));
        let magic_config_key_ctx = MockServer::magic_config_key_context();
        magic_config_key_ctx.expect()
            .return_const(Some("magic_config_key"));
        let name_ctx = MockServer::name_context();
        name_ctx.expect()
            .return_const("Mock NBD Server");
        let version_ctx = MockServer::version_context();
        version_ctx.expect()
            .return_const("3.14.159");
        unsafe {
            PLUGIN = Some(plugin_init());
        }
    });
}

fn with_fixture<F: FnMut(&mut Fixture)>(mut f: F) {
    initialize();
    let _m = MOCK_SERVER_MTX.lock().unwrap();

    let mut mock = Box::new(MockServer::default());
    mock.expect_get_size()
        .returning(|| Ok(0x4200));
    let mockp = (&mut mock) as *mut Box<MockServer>;
    let open_ctx = MockServer::open_context();
    open_ctx.expect()
        .return_once(|_| mock);

    let pluginp = unsafe { PLUGIN.unwrap()};
    let plugin = unsafe {&*pluginp};
    let handle = ((*plugin).open)(0);
    open_ctx.checkpoint();              // clear expectations for MockServer::open
    let mut fixture = Fixture {
        mockp,
        plugin,
        handle
    };

    f(&mut fixture);

    ((*plugin).close)(handle);
}

/// Helper for testing methods that take a handle and return a boolean
macro_rules! can_method {
    ( $meth:ident, $expect_meth:ident ) => {
        mod $meth {
            use super::*;

            #[test]
            fn eio() {
                with_fixture(|fixture| {
                    unsafe{ &mut *fixture.mockp }.$expect_meth()
                    .returning(|| Err(Error::new(libc::EIO, "I/O Schmerror")));
                    let $meth = fixture.plugin.$meth.as_ref().unwrap();
                    assert_eq!(-1, ($meth)(fixture.handle));
                    ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
                    ERRMSG.with(|e| assert_eq!("I/O Schmerror", *e.borrow()));
                });
            }

            #[test]
            fn ok() {
                with_fixture(|fixture| {
                    unsafe{ &mut *fixture.mockp }.$expect_meth()
                    .returning(|| Ok(true));
                    let $meth = fixture.plugin.$meth.as_ref().unwrap();
                    assert_ne!(0, ($meth)(fixture.handle));
                });
            }
        }
    }
}

/// Helper for testing methods that take a handle and return a special Flags
macro_rules! can_cache_like {
    ( $meth:ident, $flags_ty:ident, $expect_meth:ident ) => {
        mod $meth {
            use super::*;

            #[test]
            fn eio() {
                with_fixture(|fixture| {
                    unsafe{ &mut *fixture.mockp }.$expect_meth()
                    .returning(|| Err(Error::new(libc::EIO, "I/O Schmerror")));
                    let $meth = fixture.plugin.$meth.as_ref().unwrap();
                    assert_eq!(-1, ($meth)(fixture.handle));
                    ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
                    ERRMSG.with(|e| assert_eq!("I/O Schmerror", *e.borrow()));
                });
            }

            #[test]
            fn ok() {
                with_fixture(|fixture| {
                    unsafe{ &mut *fixture.mockp }.$expect_meth()
                    .returning(|| Ok($flags_ty::Emulate));
                    let $meth = fixture.plugin.$meth.as_ref().unwrap();
                    assert_eq!(1, ($meth)(fixture.handle));
                });
            }
        }
    }
}

/// Helper for testing methods with signatures like trim
macro_rules! trim_like {
    ( $meth:ident, $expect_meth:ident ) => {
        mod $meth {
            use super::*;

            #[test]
            fn eio() {
                with_fixture(|fixture| {
                    unsafe{ &mut *fixture.mockp }.$expect_meth()
                    .with(eq(42), eq(1024), eq(Flags::FUA))
                    .returning(|_, _, _| {
                        Err(Error::new(libc::EIO, "I/O Schmerror"))
                    });
                    let $meth = fixture.plugin.$meth.as_ref().unwrap();
                    assert_eq!(-1, ($meth)(fixture.handle,42, 1024, 2));
                    ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
                    ERRMSG.with(|e| assert_eq!("I/O Schmerror", *e.borrow()));
                });
            }

            #[test]
            fn ok() {
                with_fixture(|fixture| {
                    unsafe{ &mut *fixture.mockp }.$expect_meth()
                    .with(eq(42), eq(1024), eq(Flags::FUA))
                    .returning(|_, _, _| Ok(()));
                    let $meth = fixture.plugin.$meth.as_ref().unwrap();
                    assert_eq!(0, ($meth)(fixture.handle, 42, 1024, 2));
                });
            }
        }
    }
}

/// Helper for testing methods with signatures like unload
macro_rules! unload_like {
    ( $meth:ident, $ctx_meth:ident ) => {
        mod $meth {
            use super::*;

            #[test]
            fn ok() {
                initialize();
                let _m = MOCK_SERVER_MTX.lock().unwrap();

                let ctx = MockServer::$ctx_meth();
                ctx.expect()
                    .return_const(());

                let pluginp = unsafe { PLUGIN.unwrap()};
                let plugin = unsafe {&*pluginp};

                let $meth = plugin.$meth.as_ref().unwrap();
                $meth();
                ctx.checkpoint();
            }
        }
    }
}

can_cache_like!(can_cache, CacheFlags, expect_can_cache);
can_method!(can_extents, expect_can_extents);
can_method!(can_fast_zero, expect_can_fast_zero);
can_method!(can_flush, expect_can_flush);
can_cache_like!(can_fua, FuaFlags, expect_can_fua);
can_method!(can_multi_conn, expect_can_multi_conn);
can_method!(can_trim, expect_can_trim);
can_method!(can_write, expect_can_write);
can_method!(can_zero, expect_can_zero);

mod cache {
    use super::*;

    #[test]
    fn eio() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_cache()
            .with(eq(42), eq(1024))
            .returning(|_, _| {
                Err(Error::new(libc::EIO, "I/O Schmerror"))
            });
            let cache = fixture.plugin.cache.as_ref().unwrap();
            assert_eq!(-1, (cache)(fixture.handle, 42, 1024, 0));
            ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
            ERRMSG.with(|e| assert_eq!("I/O Schmerror", *e.borrow()));
        });
    }

    #[test]
    fn ok() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_cache()
            .with(eq(42), eq(1024))
            .returning(|_, _| Ok(()));
            let cache = fixture.plugin.cache.as_ref().unwrap();
            assert_eq!(0, (cache)(fixture.handle, 42, 1024, 0));
        });
    }
}

mod config {
    use super::*;

    #[test]
    fn einval() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let config_ctx = MockServer::config_context();
        config_ctx.expect()
            .with(eq("foo"), eq("bar"))
            .returning(|_, _| Err(Error::new(libc::EINVAL, "Invalid value for foo")));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let config = plugin.config.as_ref().unwrap();
        assert_eq!(
            -1,
            config(b"foo\0".as_ptr() as *const c_char,
                   b"bar\0".as_ptr() as *const c_char)
        );
        ERRNO.with(|e| assert_eq!(libc::EINVAL, *e.borrow()));
        ERRMSG.with(|e| assert_eq!("Invalid value for foo", *e.borrow()));
    }

    #[test]
    fn ok() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let config_ctx = MockServer::config_context();
        config_ctx.expect()
            .with(eq("foo"), eq("bar"))
            .returning(|_, _| Ok(()));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let config = plugin.config.as_ref().unwrap();
        assert_eq!(
            0,
            config(b"foo\0".as_ptr() as *const c_char,
                   b"bar\0".as_ptr() as *const c_char)
        );
    }
}

mod config_complete {
    use super::*;

    #[test]
    fn einval() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let config_complete_ctx = MockServer::config_complete_context();
        config_complete_ctx.expect()
            .returning(|| Err(Error::new(libc::EINVAL, "foo is required")));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let config_complete = plugin.config_complete.as_ref().unwrap();
        assert_eq!( -1, config_complete());
        ERRNO.with(|e| assert_eq!(libc::EINVAL, *e.borrow()));
        ERRMSG.with(|e| assert_eq!("foo is required", *e.borrow()));
    }

    #[test]
    fn ok() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let config_complete_ctx = MockServer::config_complete_context();
        config_complete_ctx.expect()
            .returning(|| Ok(()));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let config_complete = plugin.config_complete.as_ref().unwrap();
        assert_eq!( 0, config_complete());
    }
}

#[test]
fn config_help() {
    initialize();

    let pluginp = unsafe { PLUGIN.unwrap()};
    let plugin = unsafe {&*pluginp};

    let help = unsafe { CStr::from_ptr(plugin.config_help) };
    assert_eq!(help.to_str().unwrap(), "Mock config help");
}

#[test]
fn description() {
    initialize();

    let pluginp = unsafe { PLUGIN.unwrap()};
    let plugin = unsafe {&*pluginp};

    let description = unsafe { CStr::from_ptr(plugin.description) };
    assert_eq!(description.to_str().unwrap(), "Mock description");
}

unload_like!(dump_plugin, dump_plugin_context);

mod extents {
    use super::*;

    #[test]
    fn eopnotsupp() {
        with_fixture(|fixture| {
            let msg = "Backing store does not support extents";
            unsafe{ &mut *fixture.mockp }.expect_extents()
            .with(eq(512), eq(10240), eq(Flags::empty()), always())
            .returning(move |_, _, _, _| Err(Error::new(libc::EOPNOTSUPP, msg)));
            let extents = fixture.plugin.extents.as_ref().unwrap();
            assert_eq!(-1, (extents)(fixture.handle,
                                     512,
                                     10240,
                                     0,
                                     ptr::null_mut()));
            ERRNO.with(|e| assert_eq!(libc::EOPNOTSUPP, *e.borrow()));
            ERRMSG.with(|e| assert_eq!(msg, *e.borrow()));
        });
    }

    #[test]
    fn extent_error() {
        with_fixture(|fixture| {
            let ctx = MockNbdkitAddExtent::add_context();
            ctx.expect()
                .with(always(), eq(8192), eq(4096), eq(2))
                .returning(|_, _, _, _| {
                    set_errno(Errno(libc::ERANGE));
                    -1
                });

            unsafe{ &mut *fixture.mockp }.expect_extents()
            .with(eq(512), eq(10240), eq(Flags::empty()), always())
            .returning(|_, _, _, exh| {
                exh.add(8192, 4096, ExtentType::Zero)
            });
            let extents = fixture.plugin.extents.as_ref().unwrap();
            assert_eq!(-1, (extents)(fixture.handle,
                                     512,
                                     10240,
                                     0,
                                     ptr::null_mut()));
            ERRNO.with(|e| assert_eq!(libc::ERANGE, *e.borrow()));
            ctx.checkpoint();
        });
    }

    #[test]
    fn req_one() {
        with_fixture(|fixture| {
            let ctx = MockNbdkitAddExtent::add_context();
            ctx.expect()
                .with(always(), eq(8192), eq(4096), eq(2))
                .return_const(0);

            unsafe{ &mut *fixture.mockp }.expect_extents()
            .with(eq(512), eq(10240), eq(Flags::REQ_ONE), always())
            .returning(|_, _, _, exh| {
                exh.add(8192, 4096, ExtentType::Zero).unwrap();
                Ok(())
            });
            let extents = fixture.plugin.extents.as_ref().unwrap();
            assert_eq!(0, (extents)(fixture.handle,
                                    512,
                                    10240,
                                    4,
                                    ptr::null_mut()));

            ctx.checkpoint();
        });
    }
}

mod flush {
    use super::*;

    #[test]
    fn eio() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_flush()
            .returning(|| Err(Error::new(libc::EIO, "I/O Schmerror")));
            let flush = fixture.plugin.flush.as_ref().unwrap();
            assert_eq!(-1, (flush)(fixture.handle, 0));
            ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
            ERRMSG.with(|e| assert_eq!("I/O Schmerror", *e.borrow()));
        });
    }

    #[test]
    fn ok() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_flush()
            .returning(|| Ok(()));
            let flush = fixture.plugin.flush.as_ref().unwrap();
            assert_eq!(0, (flush)(fixture.handle, 0));
        });
    }
}

mod get_ready {
    use super::*;

    #[test]
    fn einval() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let get_ready_ctx = MockServer::get_ready_context();
        get_ready_ctx.expect()
            .returning(|| Err(Error::new(libc::EINVAL, "foo is required")));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let get_ready = plugin.get_ready.as_ref().unwrap();
        assert_eq!( -1, get_ready());
        ERRNO.with(|e| assert_eq!(libc::EINVAL, *e.borrow()));
        ERRMSG.with(|e| assert_eq!("foo is required", *e.borrow()));
    }

    #[test]
    fn ok() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let get_ready_ctx = MockServer::get_ready_context();
        get_ready_ctx.expect()
            .returning(|| Ok(()));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let get_ready = plugin.get_ready.as_ref().unwrap();
        assert_eq!( 0, get_ready());
    }
}

can_method!(is_rotational, expect_is_rotational);

unload_like!(load, load_context);

#[test]
fn longname() {
    initialize();

    let pluginp = unsafe { PLUGIN.unwrap()};
    let plugin = unsafe {&*pluginp};

    let longname = unsafe { CStr::from_ptr(plugin.longname) };
    assert_eq!(longname.to_str().unwrap(), "Mock long name");
}

#[test]
fn magic_config_key() {
    initialize();

    let pluginp = unsafe { PLUGIN.unwrap()};
    let plugin = unsafe {&*pluginp};

    let magic_config_key = unsafe { CStr::from_ptr(plugin.magic_config_key) };
    assert_eq!(magic_config_key.to_str().unwrap(), "magic_config_key");
}

#[test]
fn name() {
    initialize();

    let pluginp = unsafe { PLUGIN.unwrap()};
    let plugin = unsafe {&*pluginp};

    let name = unsafe { CStr::from_ptr(plugin.name) };
    assert_eq!(name.to_str().unwrap(), "Mock NBD Server");
}

mod preconnect {
    use super::*;

    #[test]
    fn einval() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let preconnect_ctx = MockServer::preconnect_context();
        preconnect_ctx.expect()
            .returning(|_| Err(Error::new(libc::EINVAL, "foo is required")));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let preconnect = plugin.preconnect.as_ref().unwrap();
        assert_eq!( -1, preconnect(0));
        ERRNO.with(|e| assert_eq!(libc::EINVAL, *e.borrow()));
        ERRMSG.with(|e| assert_eq!("foo is required", *e.borrow()));
    }

    #[test]
    fn ok() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let preconnect_ctx = MockServer::preconnect_context();
        preconnect_ctx.expect()
            .with(eq(true))
            .returning(|_| Ok(()));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let preconnect = plugin.preconnect.as_ref().unwrap();
        assert_eq!( 0, preconnect(1));
    }
}

mod pwrite {
    use super::*;

    #[test]
    fn eio() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_write_at()
            .with(eq(&b"0123456789"[..]), eq(1024), eq(Flags::empty()))
            .returning(|_, _, _| Err(Error::new(libc::EIO, "I/O Schmerror")));
            let buf = b"0123456789";
            let pwrite = fixture.plugin.pwrite.as_ref().unwrap();
            assert_eq!(-1, (pwrite)(fixture.handle,
                                    buf.as_ptr() as *mut c_char,
                                    buf.len() as u32,
                                    1024,
                                    0));
            ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
            ERRMSG.with(|e| assert_eq!("I/O Schmerror", *e.borrow()));
        });
    }

    #[test]
    fn ok() {
        with_fixture(|fixture| {
            unsafe{ &mut *fixture.mockp }.expect_write_at()
            .with(eq(&b"0123456789"[..]), eq(1024), eq(Flags::FUA))
            .returning(|_, _, _| Ok(()));
            let buf = b"0123456789";
            let pwrite = fixture.plugin.pwrite.as_ref().unwrap();
            assert_eq!(0, (pwrite)(fixture.handle,
                                   buf.as_ptr() as *mut c_char,
                                   buf.len() as u32,
                                   1024,
                                   2));
        });
    }
}

mod thread_model {
    use super::*;

    #[test]
    fn eio() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let thread_model_ctx = MockServer::thread_model_context();
        thread_model_ctx.expect()
            .returning(|| Err(Error::new(libc::EIO, "Mock error")));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        assert_eq!(-1, (plugin.thread_model.as_ref().unwrap())());
        ERRNO.with(|e| assert_eq!(libc::EIO, *e.borrow()));
        ERRMSG.with(|e| assert_eq!("Mock error", *e.borrow()));
    }

    #[test]
    fn ok() {
        initialize();
        let _m = MOCK_SERVER_MTX.lock().unwrap();

        let thread_model_ctx = MockServer::thread_model_context();
        thread_model_ctx.expect()
            .returning(|| Ok(ThreadModel::SerializeAllRequests ));

        let pluginp = unsafe { PLUGIN.unwrap()};
        let plugin = unsafe {&*pluginp};

        let thread_model = (plugin.thread_model.as_ref().unwrap())();
        assert_eq!(thread_model, 1);
    }
}

trim_like!(trim, expect_trim);
unload_like!(unload, unload_context);
trim_like!(zero, expect_zero);
