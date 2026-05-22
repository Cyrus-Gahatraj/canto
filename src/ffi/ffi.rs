use std::ffi::c_char;

unsafe extern "C" {
    pub fn compile(source: *const c_char, file_path: *const c_char, output_path: *const c_char);
}
