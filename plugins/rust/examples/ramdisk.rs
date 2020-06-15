use lazy_static::lazy_static;
use std::sync::Mutex;
use nbdkit::*;

// The RAM disk.
lazy_static! {
    static ref DISK: Mutex<Vec<u8>> = Mutex::new (vec![0; 100 * 1024 * 1024]);
}

#[derive(Default)]
struct RamDisk {
    // Box::new doesn't allocate anything unless we put some dummy
    // fields here.  In a real implementation you would put per-handle
    // data here as required.
    _not_used: i32,
}

impl Server for RamDisk {
    fn get_size(&self) -> Result<i64> {
        Ok(DISK.lock().unwrap().len() as i64)
    }

    fn name() -> &'static str {
        "ramdisk"
    }

    fn open(_readonly: bool) -> Box<dyn Server> {
        Box::new(RamDisk::default())
    }

    fn read_at(&self, buf: &mut [u8], offset: u64) -> Result<()> {
        let disk = DISK.lock().unwrap();
        let ofs = offset as usize;
        let end = ofs + buf.len();
        buf.copy_from_slice(&disk[ofs..end]);
        Ok(())
    }

    fn thread_model() -> Result<ThreadModel> where Self: Sized {
        Ok(ThreadModel::Parallel)
    }

    fn write_at(&self, buf: &[u8], offset: u64, _flags: Flags) -> Result<()> {
        let mut disk = DISK.lock().unwrap();
        let ofs = offset as usize;
        let end = ofs + buf.len();
        disk[ofs..end].copy_from_slice(buf);
        Ok(())
    }
}

plugin!(RamDisk {thread_model, write_at});
