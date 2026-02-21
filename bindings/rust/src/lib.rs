use libc::{c_int, c_void, size_t, c_ulonglong, c_char};
use std::ffi::CStr;
use std::slice;

extern "C" {
    fn mcx_compress_bound(src_size: size_t) -> size_t;
    fn mcx_compress(dst: *mut c_void, dst_cap: size_t, src: *const c_void, src_size: size_t, level: c_int) -> size_t;
    fn mcx_decompress(dst: *mut c_void, dst_cap: size_t, src: *const c_void, src_size: size_t) -> size_t;
    fn mcx_get_decompressed_size(src: *const c_void, src_size: size_t) -> c_ulonglong;
    fn mcx_is_error(result: size_t) -> c_int;
    fn mcx_get_error_name(result: size_t) -> *const c_char;
}

/// Compresses a byte slice using the MaxCompression algorithm.
/// 
/// `level` ranges from 1 (fastest) to 22 (best), with 3 as the default.
pub fn compress(data: &[u8], level: i32) -> Result<Vec<u8>, String> {
    if data.is_empty() {
        return Ok(Vec::new());
    }

    let src_size = data.len() as size_t;
    
    // Safety: we bound the requirement precisely from the C API
    let dst_cap = unsafe { mcx_compress_bound(src_size) };
    let mut dst_buffer: Vec<u8> = vec![0; dst_cap as usize];

    let result = unsafe {
        mcx_compress(
            dst_buffer.as_mut_ptr() as *mut c_void,
            dst_cap,
            data.as_ptr() as *const c_void,
            src_size,
            level as c_int,
        )
    };

    if unsafe { mcx_is_error(result) } != 0 {
        let err_ptr = unsafe { mcx_get_error_name(result) };
        let err_str = unsafe { CStr::from_ptr(err_ptr) }.to_string_lossy().into_owned();
        return Err(format!("Compression failed: {}", err_str));
    }

    dst_buffer.truncate(result as usize);
    Ok(dst_buffer)
}

/// Decompresses a `.mcx` encoded byte slice.
pub fn decompress(data: &[u8]) -> Result<Vec<u8>, String> {
    if data.is_empty() {
        return Ok(Vec::new());
    }

    let src_size = data.len() as size_t;
    
    let orig_size = unsafe { mcx_get_decompressed_size(data.as_ptr() as *const c_void, src_size) };
    if orig_size == 0 {
        return Err("Decompression failed: Could not determine original size from header".to_string());
    }

    let mut dst_buffer: Vec<u8> = vec![0; orig_size as usize];

    let result = unsafe {
        mcx_decompress(
            dst_buffer.as_mut_ptr() as *mut c_void,
            orig_size as size_t,
            data.as_ptr() as *const c_void,
            src_size,
        )
    };

    if unsafe { mcx_is_error(result) } != 0 {
        let err_ptr = unsafe { mcx_get_error_name(result) };
        let err_str = unsafe { CStr::from_ptr(err_ptr) }.to_string_lossy().into_owned();
        return Err(format!("Decompression failed: {}", err_str));
    }

    if result as c_ulonglong != orig_size {
        return Err(format!("Decompression length mismatch: expected {}, got {}", orig_size, result));
    }

    Ok(dst_buffer)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_roundtrip() {
        let original = b"MaxCompression Rust Bindings Test Data!".repeat(100);
        let compressed = compress(&original, 10).expect("Compression failed");
        let decompressed = decompress(&compressed).expect("Decompression failed");
        assert_eq!(original.to_vec(), decompressed);
    }
}
