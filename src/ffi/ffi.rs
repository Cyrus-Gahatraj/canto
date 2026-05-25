use std::ffi::c_char;

// mirror C struct
#[repr(C)]
pub struct CantoContextOpaque {
    _unused: [u8; 0],
}

unsafe extern "C" {
    pub fn canto_ctx_create(is_repl: bool) -> *mut CantoContextOpaque;
    pub fn canto_ctx_free(ctx: *mut CantoContextOpaque);
    pub fn compile(
        ctx: *mut CantoContextOpaque,
        source: *const c_char,
        file_path: *const c_char,
        output_path: *const c_char,
    ) -> bool;
}

