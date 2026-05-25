mod ffi;

use std::ffi::CString;
use std::path::Path;

use crate::ffi::ffi::CantoContextOpaque;

pub struct Engine{
    raw_ctx: *mut CantoContextOpaque,
}

fn path_to_cstring(path: Option<&Path>) -> Option<CString> {
    path.and_then(|p| p.to_str())
        .and_then(|s| CString::new(s).ok())
}

impl Engine {
    pub fn new(is_repl: bool) -> Self {
        let ctx_ptr = unsafe { ffi::canto_ctx_create(is_repl) };
        if ctx_ptr.is_null() {
            panic!("Fatal: Failed to allocate CantoContext in C heap.");
        }
        Engine { raw_ctx: ctx_ptr }
    }

    pub fn run(&mut self, source: &str, file_path: Option<&Path>, output_path: Option<&Path>) -> bool {
        let c_source = CString::new(source).expect("Null byte in source");
        let c_path = path_to_cstring(file_path);
        let out_path = path_to_cstring(output_path);

        unsafe {
            let path_ptr = c_path.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null());
            let output_ptr = out_path.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null());
            
            ffi::compile(self.raw_ctx, c_source.as_ptr(), path_ptr, output_ptr)
        }
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        unsafe {
            if !self.raw_ctx.is_null() {
                ffi::canto_ctx_free(self.raw_ctx);
            }
        }
    }
}

