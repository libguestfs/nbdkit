//vim: tw=80
use lazy_static::lazy_static;
use mockall::mock;
use std::{
    cell::RefCell,
    ffi::{CString, CStr},
    os::raw::{c_char, c_int, c_void},
    sync::Mutex
};

use nbdkit::*;

thread_local!(pub static ERRMSG: RefCell<String> = RefCell::new(String::new()));
thread_local!(pub static ERRNO: RefCell<i32> = RefCell::new(0));

lazy_static! {
    /// Mediates access to MockServer's global expectations
    /// Any test that sets an expectation on a static method (like `open`) must
    /// grab this mutex
    pub static ref MOCK_SERVER_MTX: Mutex<()> = Mutex::new(());
}

mock!{
    pub Server {}
    trait Server {
        fn cache(&self, count: u32, offset: u64) -> Result<()>;
        fn can_cache(&self) -> Result<CacheFlags>;
        fn can_extents(&self) -> Result<bool>;
        fn can_fast_zero(&self) -> Result<bool>;
        fn can_flush(&self) -> Result<bool>;
        fn can_fua(&self) -> Result<FuaFlags>;
        fn can_multi_conn(&self) -> Result<bool>;
        fn can_trim(&self) -> Result<bool>;
        fn can_write(&self) -> Result<bool>;
        fn can_zero(&self) -> Result<bool>;
        fn config(k: &str, v: &str) -> Result<()> where Self: Sized;
        fn config_complete() -> Result<()> where Self: Sized;
        fn config_help() -> Option<&'static str> where Self: Sized;
        fn description() -> Option<&'static str> where Self: Sized;
        fn dump_plugin() where Self: Sized;
        fn extents(&self, c: u32, o: u64, f: Flags, e: &mut ExtentHandle)
            -> Result<()>;
        fn flush(&self) -> Result<()>;
        fn get_ready() -> Result<()> where Self: Sized;
        fn get_size(&self) -> Result<i64>;
        fn is_rotational(&self) -> Result<bool>;
        fn load() where Self: Sized;
        fn longname() -> Option<&'static str> where Self: Sized;
        fn magic_config_key() -> Option<&'static str> where Self: Sized;
        fn name() -> &'static str where Self: Sized;
        fn open(readonly: bool) -> Box<dyn Server> where Self: Sized;
        fn preconnect(readonly: bool) -> Result<()> where Self: Sized;
        fn read_at(&self, buf: &mut [u8], offset: u64) -> Result<()>;
        fn thread_model() -> Result<ThreadModel> where Self: Sized;
        fn trim(&self, count: u32, offset: u64, flags: Flags) -> Result<()>;
        fn unload() where Self: Sized;
        fn version() -> Option<&'static str> where Self: Sized;
        fn write_at(&self, _buf: &[u8], _offset: u64, _flags: Flags) -> Result<()>;
        fn zero(&self, count: u32, offset: u64, flags: Flags) -> Result<()>;
    }
}

mock! {
    pub NbdkitAddExtent {
        fn add(extents: *mut c_void, offset: u64, len: u64, ty: u32) -> c_int;

    }
}

#[no_mangle]
extern fn nbdkit_add_extent(extents: *mut c_void, offset: u64, len: u64, ty: u32)
    -> c_int
{
    MockNbdkitAddExtent::add(extents, offset, len, ty)
}

// XXX The actual C function is variadic, but stable Rust can't create variadic
// extern functions yet.  So we pretend that it has two arguments.
// https://github.com/rust-lang/rust/issues/44930
// extern fn nbdkit_error(fmt: *const c_char, ...);
#[no_mangle]
extern fn nbdkit_error(fmt: *const c_char, msg: *const c_char) {
    assert_eq!(
        unsafe {CStr::from_ptr(fmt) },
        CString::new("%s").unwrap().as_c_str()
    );
    ERRMSG.with(|m| *m.borrow_mut() = unsafe {
            CStr::from_ptr(msg)
        }.to_str()
        .unwrap()
        .to_owned()
    );
}
#[no_mangle]
extern fn nbdkit_set_error(errno: c_int) {
    ERRNO.with(|e| *e.borrow_mut() = errno);
}

/// Holds common setup stuff needed by most test cases
pub struct Fixture<'a> {
    pub mockp: *mut Box<MockServer>,
    pub plugin: &'a Plugin,
    pub handle: *mut c_void
}
