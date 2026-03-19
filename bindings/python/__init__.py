"""MaxCompression Python bindings."""
from .maxcomp import compress, decompress, compress_bound, version, get_frame_info, MaxCompressionError

__version__ = "2.1.1"
__all__ = ["compress", "decompress", "compress_bound", "version", "get_frame_info", "MaxCompressionError"]
