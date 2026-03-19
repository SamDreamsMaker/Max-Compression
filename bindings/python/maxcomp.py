"""
maxcomp — Python bindings for MaxCompression (MCX).

High-ratio lossless data compression via ctypes. See README.md for examples.
"""

import ctypes
import os
import sys
from typing import Optional

__all__ = [
    "compress",
    "decompress",
    "compress_bound",
    "get_frame_info",
    "version",
    "MaxCompressionError",
]

__version__ = "2.1.1"

# Platform-specific library extension
if sys.platform.startswith('win32'):
    LIB_EXT = '.dll'
    LIB_PREFIX = ''
elif sys.platform.startswith('darwin'):
    LIB_EXT = '.dylib'
    LIB_PREFIX = 'lib'
else:
    LIB_EXT = '.so'
    LIB_PREFIX = 'lib'

def _load_library() -> ctypes.CDLL:
    """Attempt to locate and load the maxcomp shared library."""
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Possible locations to search for the built dynamic library
    search_paths = [
        os.path.join(current_dir, f"{LIB_PREFIX}maxcomp{LIB_EXT}"),
        os.path.join(current_dir, "..", "..", "build", "lib", "Release", f"{LIB_PREFIX}maxcomp{LIB_EXT}"),
        os.path.join(current_dir, "..", "..", "build", "lib", f"{LIB_PREFIX}maxcomp{LIB_EXT}"),
        os.path.abspath(os.path.join(current_dir, "..", "..", "build", "bin", "Release", "maxcomp.dll")) # MSVC specific
    ]
    
    for path in search_paths:
        if os.path.exists(path):
            try:
                if sys.platform.startswith('win32'):
                    return ctypes.CDLL(path, winmode=0)
                return ctypes.CDLL(path)
            except Exception as e:
                import warnings
                warnings.warn(f"Found {path} but failed to load: {e}")
                
    raise RuntimeError(f"Could not find MaxCompression shared library {LIB_PREFIX}maxcomp{LIB_EXT} in search paths.")

_lib = _load_library()

# Function signatures
_lib.mcx_compress_bound.argtypes = [ctypes.c_size_t]
_lib.mcx_compress_bound.restype = ctypes.c_size_t

_lib.mcx_compress.argtypes = [
    ctypes.c_void_p,  # dst
    ctypes.c_size_t,  # dst_cap
    ctypes.c_void_p,  # src
    ctypes.c_size_t,  # src_size
    ctypes.c_int      # level
]
_lib.mcx_compress.restype = ctypes.c_size_t

_lib.mcx_decompress.argtypes = [
    ctypes.c_void_p,  # dst
    ctypes.c_size_t,  # dst_cap
    ctypes.c_void_p,  # src
    ctypes.c_size_t   # src_size
]
_lib.mcx_decompress.restype = ctypes.c_size_t

_lib.mcx_get_decompressed_size.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
_lib.mcx_get_decompressed_size.restype = ctypes.c_ulonglong

_lib.mcx_is_error.argtypes = [ctypes.c_size_t]
_lib.mcx_is_error.restype = ctypes.c_int

_lib.mcx_get_error_name.argtypes = [ctypes.c_size_t]
_lib.mcx_get_error_name.restype = ctypes.c_char_p

class MaxCompressionError(Exception):
    """Exception raised when compression/decompression fails in the native layer."""
    pass

def compress(data: bytes, level: int = 3) -> bytes:
    """
    Compress a bytes object using MaxCompression.
    
    Args:
        data: The input bytes to compress.
        level: Compression level (1=FASTEST, 3=DEFAULT, 22=BEST).
        
    Returns:
        A new bytes object containing the `.mcx` encoded frame.
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("Input data must be bytes or bytearray")
        
    src_size = len(data)
    if src_size == 0:
        return b""
        
    # Get required destination capacity
    dst_cap = _lib.mcx_compress_bound(src_size)
    dst_buffer = (ctypes.c_ubyte * dst_cap)()
    
    # Compress
    result = _lib.mcx_compress(
        ctypes.addressof(dst_buffer),
        dst_cap,
        data,
        src_size,
        level
    )
    
    if _lib.mcx_is_error(result):
        err_msg = _lib.mcx_get_error_name(result).decode('utf-8')
        raise MaxCompressionError(f"Compression failed: {err_msg}")
        
    # Extract only the exact compressed bytes
    return bytes(dst_buffer[:result])

def decompress(data: bytes) -> bytes:
    """
    Decompress a `.mcx` encoded bytes object using MaxCompression.
    
    Args:
        data: The compressed bytes object.
        
    Returns:
        A new bytes object containing the original uncompressed data.
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("Input data must be bytes or bytearray")
        
    src_size = len(data)
    if src_size == 0:
        return b""
        
    # Parse original size from frame header
    orig_size = _lib.mcx_get_decompressed_size(data, src_size)
    if orig_size == 0:
        raise MaxCompressionError("Decompression failed: Could not determine original size from header")
        
    # Allocate exact required space
    dst_buffer = (ctypes.c_ubyte * orig_size)()
    
    # Decompress
    result = _lib.mcx_decompress(
        ctypes.addressof(dst_buffer),
        orig_size,
        data,
        src_size
    )
    
    if _lib.mcx_is_error(result):
        err_msg = _lib.mcx_get_error_name(result).decode('utf-8')
        raise MaxCompressionError(f"Decompression failed: {err_msg}")
        
    if result != orig_size:
        raise MaxCompressionError(f"Decompression length mismatch: expected {orig_size}, got {result}")
        
    return bytes(dst_buffer)


def version() -> str:
    """Return the MaxCompression library version string."""
    try:
        _lib.mcx_version_string.restype = ctypes.c_char_p
        return _lib.mcx_version_string().decode('utf-8')
    except Exception:
        return "unknown"


def compress_bound(size: int) -> int:
    """Return the maximum compressed size for a given input size."""
    return _lib.mcx_compress_bound(size)


def get_frame_info(data: bytes) -> dict:
    """
    Read frame metadata from compressed data without decompressing.
    
    Args:
        data: The compressed bytes object (only header is read).
        
    Returns:
        A dict with keys: original_size, version, level, strategy, flags.
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("Input data must be bytes or bytearray")
    
    class MCXFrameInfo(ctypes.Structure):
        _fields_ = [
            ("original_size", ctypes.c_ulonglong),
            ("version", ctypes.c_uint),
            ("level", ctypes.c_uint),
            ("strategy", ctypes.c_uint),
            ("flags", ctypes.c_uint),
        ]
    
    _lib.mcx_get_frame_info.argtypes = [
        ctypes.POINTER(MCXFrameInfo),
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    _lib.mcx_get_frame_info.restype = ctypes.c_size_t
    
    info = MCXFrameInfo()
    result = _lib.mcx_get_frame_info(ctypes.byref(info), data, len(data))
    
    if _lib.mcx_is_error(result):
        err_msg = _lib.mcx_get_error_name(result).decode('utf-8')
        raise MaxCompressionError(f"Failed to read frame info: {err_msg}")
    
    return {
        "original_size": info.original_size,
        "version": info.version,
        "level": info.level,
        "strategy": info.strategy,
        "flags": info.flags,
    }
