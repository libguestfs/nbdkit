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
        Box::<RamDisk>::default()
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
