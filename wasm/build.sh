#!/bin/bash
# Build MaxCompression as WASM module
# Requires: emscripten SDK (source emsdk_env.sh first)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "Building MCX static library with Emscripten..."
mkdir -p "$PROJECT_DIR/build-wasm"
cd "$PROJECT_DIR/build-wasm"

emcmake cmake -S "$PROJECT_DIR" -B . \
  -DCMAKE_BUILD_TYPE=Release \
  -DMCX_BUILD_CLI=OFF \
  -DMCX_BUILD_TESTS=OFF 2>&1

emmake make maxcomp_static -j$(nproc) 2>&1

echo ""
echo "Generating WASM module..."
emcc -O3 -s WASM=1 \
  -s EXPORTED_FUNCTIONS='["_mcx_compress","_mcx_decompress","_mcx_compress_bound","_mcx_version_string","_mcx_is_error","_mcx_get_decompressed_size","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME='MaxCompression' \
  -I "$PROJECT_DIR/include" \
  lib/libmaxcomp.a \
  -o "$SCRIPT_DIR/maxcomp.js"

echo ""
echo "Output:"
ls -lh "$SCRIPT_DIR/maxcomp.js" "$SCRIPT_DIR/maxcomp.wasm"
echo ""
echo "Done! Use: const MCX = require('./wasm/maxcomp.js')"
