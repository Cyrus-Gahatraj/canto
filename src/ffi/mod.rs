mod ffi;

use std::ffi::CString;
use std::path::Path;

pub struct Engine;

fn path_to_cstring(path: Option<&Path>) -> Option<CString> {
    path.and_then(|p| p.to_str())
        .and_then(|s| CString::new(s).ok())
}

impl Engine {
    pub fn new() -> Self { Engine }

    pub fn run(&self, source: &str, file_path: Option<&Path>, output_path: Option<&Path>) {
        let c_source = CString::new(source).expect("Null byte in source");
        
        // Convert Path to C-compatible string
        let c_path =  path_to_cstring(file_path);
        let out_path = path_to_cstring(output_path);

        unsafe {
            // Pass pointers to C
            // if c_path is None, we pass null_ptr
            let path_ptr = c_path.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null());
            let output_ptr = out_path.as_ref().map(|c| c.as_ptr()).unwrap_or(std::ptr::null());
            
            ffi::compile(c_source.as_ptr(), path_ptr, output_ptr);
        }
    }
}
