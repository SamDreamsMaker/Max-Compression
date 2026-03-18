"""MaxCompression Python bindings."""
from .maxcomp import compress, decompress, MaxCompressionError

__version__ = "1.9.3"
__all__ = ["compress", "decompress", "MaxCompressionError"]
