use libc::{c_int, c_uint, c_void, size_t, c_ulonglong, c_char};
use std::ffi::CStr;
use std::slice;

/* ─── C FFI declarations ─────────────────────────────────────────────── */

#[repr(C)]
struct CFrameInfo {
    original_size: c_ulonglong,
    version:       c_uint,
    level:         c_uint,
    strategy:      c_uint,
    flags:         c_uint,
}

extern "C" {
    fn mcx_compress_bound(src_size: size_t) -> size_t;
    fn mcx_compress(dst: *mut c_void, dst_cap: size_t, src: *const c_void, src_size: size_t, level: c_int) -> size_t;
    fn mcx_decompress(dst: *mut c_void, dst_cap: size_t, src: *const c_void, src_size: size_t) -> size_t;
    fn mcx_get_decompressed_size(src: *const c_void, src_size: size_t) -> c_ulonglong;
    fn mcx_is_error(result: size_t) -> c_int;
    fn mcx_get_error_name(result: size_t) -> *const c_char;
    fn mcx_version_string() -> *const c_char;
    fn mcx_version_number() -> c_uint;
    fn mcx_get_frame_info(info: *mut CFrameInfo, src: *const c_void, src_size: size_t) -> size_t;
}

/* ─── Public types ───────────────────────────────────────────────────── */

/// Frame metadata read from a compressed MCX buffer.
#[derive(Debug, Clone)]
pub struct FrameInfo {
    /// Decompressed size (0 if unknown / streaming).
    pub original_size: u64,
    /// Format version (currently 1).
    pub version: u32,
    /// Compression level used (1–26).
    pub level: u32,
    /// Strategy ID (0=Store, 1=Default, 2=LZ_FAST, 3=LZ_HC, 4=BWT, 6=Babel, 7=Stride, 8=LZ24, 9=LZRC).
    pub strategy: u32,
    /// Frame flags bitfield.
    pub flags: u32,
}

/// Error type for MCX operations.
#[derive(Debug)]
pub struct MaxCompressionError(pub String);

impl std::fmt::Display for MaxCompressionError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl std::error::Error for MaxCompressionError {}

/* ─── Helper ─────────────────────────────────────────────────────────── */

/// Check if a C API result is an error, and if so return the message.
pub fn check_error(result: size_t) -> Result<size_t, MaxCompressionError> {
    if unsafe { mcx_is_error(result) } != 0 {
        let err_ptr = unsafe { mcx_get_error_name(result) };
        let msg = unsafe { CStr::from_ptr(err_ptr) }.to_string_lossy().into_owned();
        Err(MaxCompressionError(msg))
    } else {
        Ok(result)
    }
}

/* ─── Public API ─────────────────────────────────────────────────────── */

/// Returns the library version string (e.g. `"2.1.1"`).
pub fn version() -> String {
    unsafe { CStr::from_ptr(mcx_version_string()) }
        .to_string_lossy()
        .into_owned()
}

/// Returns the library version as a packed integer: `major*10000 + minor*100 + patch`.
pub fn version_number() -> u32 {
    unsafe { mcx_version_number() }
}

/// Read frame metadata from a compressed buffer without decompressing.
pub fn get_frame_info(data: &[u8]) -> Result<FrameInfo, MaxCompressionError> {
    if data.is_empty() {
        return Err(MaxCompressionError("Empty input".to_string()));
    }
    let mut c_info: CFrameInfo = CFrameInfo {
        original_size: 0,
        version: 0,
        level: 0,
        strategy: 0,
        flags: 0,
    };
    let result = unsafe {
        mcx_get_frame_info(
            &mut c_info as *mut CFrameInfo,
            data.as_ptr() as *const c_void,
            data.len() as size_t,
        )
    };
    check_error(result)?;
    Ok(FrameInfo {
        original_size: c_info.original_size as u64,
        version:       c_info.version as u32,
        level:         c_info.level as u32,
        strategy:      c_info.strategy as u32,
        flags:         c_info.flags as u32,
    })
}

/// Compresses a byte slice using the MaxCompression algorithm.
///
/// `level` ranges from 1 (fastest) to 26 (best), with 3 as the default.
pub fn compress(data: &[u8], level: i32) -> Result<Vec<u8>, MaxCompressionError> {
    if data.is_empty() {
        return Ok(Vec::new());
    }

    let src_size = data.len() as size_t;
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

    let size = check_error(result)?;
    dst_buffer.truncate(size as usize);
    Ok(dst_buffer)
}

/// Decompresses a `.mcx` encoded byte slice.
pub fn decompress(data: &[u8]) -> Result<Vec<u8>, MaxCompressionError> {
    if data.is_empty() {
        return Ok(Vec::new());
    }

    let src_size = data.len() as size_t;

    let orig_size = unsafe { mcx_get_decompressed_size(data.as_ptr() as *const c_void, src_size) };
    if orig_size == 0 {
        return Err(MaxCompressionError(
            "Could not determine original size from header".to_string(),
        ));
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

    let size = check_error(result)?;
    if size as c_ulonglong != orig_size {
        return Err(MaxCompressionError(format!(
            "Decompression length mismatch: expected {}, got {}",
            orig_size, size
        )));
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

    #[test]
    fn test_version() {
        let v = version();
        assert!(!v.is_empty(), "Version string should not be empty");
        assert!(v.contains('.'), "Version should contain a dot: {}", v);
        let n = version_number();
        assert!(n >= 20000, "Version number should be >= 2.0.0: {}", n);
    }

    #[test]
    fn test_frame_info() {
        let original = b"Frame info test data for MCX Rust bindings".repeat(50);
        let compressed = compress(&original, 6).expect("Compression failed");
        let info = get_frame_info(&compressed).expect("get_frame_info failed");
        assert_eq!(info.original_size, original.len() as u64);
        assert_eq!(info.version, 1);
        assert_eq!(info.level, 6);
    }

    #[test]
    fn test_check_error() {
        // A normal size value should not be an error
        assert!(check_error(42).is_ok());
        assert_eq!(check_error(42).unwrap(), 42);
    }
}
