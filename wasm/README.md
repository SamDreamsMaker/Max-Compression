# MaxCompression — WebAssembly Build

Compile MCX to WebAssembly for use in browsers and Node.js.

## Prerequisites

Install [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

## Build

```bash
cd max-compression
mkdir -p build-wasm && cd build-wasm
emcmake cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release -DMCX_BUILD_CLI=OFF -DMCX_BUILD_TESTS=OFF
emmake make maxcomp_static -j$(nproc)
```

## Generate JS/WASM module

```bash
emcc -O3 -s WASM=1 \
  -s EXPORTED_FUNCTIONS='["_mcx_compress","_mcx_decompress","_mcx_compress_bound","_mcx_version_string","_mcx_is_error","_mcx_get_decompressed_size","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME='MaxCompression' \
  -I ../include \
  lib/libmaxcomp.a \
  -o ../wasm/maxcomp.js
```

## Usage (Node.js)

```javascript
const MaxCompression = require('./maxcomp.js');

MaxCompression().then(mcx => {
  const compress = mcx.cwrap('mcx_compress', 'number',
    ['number', 'number', 'number', 'number', 'number']);
  const decompress = mcx.cwrap('mcx_decompress', 'number',
    ['number', 'number', 'number', 'number']);
  const compressBound = mcx.cwrap('mcx_compress_bound', 'number', ['number']);
  const version = mcx.cwrap('mcx_version_string', 'string', []);

  console.log('MCX version:', version());

  // Compress
  const input = new TextEncoder().encode('Hello, MaxCompression WASM!');
  const srcPtr = mcx._malloc(input.length);
  mcx.HEAPU8.set(input, srcPtr);

  const bound = compressBound(input.length);
  const dstPtr = mcx._malloc(bound);

  const compSize = compress(dstPtr, bound, srcPtr, input.length, 6);
  console.log(`Compressed: ${input.length} → ${compSize} bytes`);

  // Decompress
  const decPtr = mcx._malloc(input.length + 1024);
  const decSize = decompress(decPtr, input.length + 1024, dstPtr, compSize);
  const result = mcx.HEAPU8.slice(decPtr, decPtr + decSize);
  console.log('Roundtrip:', new TextDecoder().decode(result));

  mcx._free(srcPtr);
  mcx._free(dstPtr);
  mcx._free(decPtr);
});
```

## Usage (Browser)

```html
<script src="maxcomp.js"></script>
<script>
MaxCompression().then(mcx => {
  // Same API as Node.js example above
  const version = mcx.cwrap('mcx_version_string', 'string', []);
  console.log('MCX WASM loaded:', version());
});
</script>
```

## Notes

- Memory growth is enabled (`ALLOW_MEMORY_GROWTH=1`) for large files
- No OpenMP in WASM — single-threaded only
- For SharedArrayBuffer + threads, add `-s USE_PTHREADS=1` (requires COOP/COEP headers)
- File size: ~60-80KB gzipped (typical)
