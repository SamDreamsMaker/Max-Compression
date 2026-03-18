"""MaxCompression Python bindings."""
from .maxcomp import compress, decompress, compress_bound, version, MaxCompressionError

__version__ = "2.0.0"
__all__ = ["compress", "decompress", "compress_bound", "version", "MaxCompressionError"]
