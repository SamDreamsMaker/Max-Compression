# ─── MaxCompression Tests ────────────────────────────────────────────

# Round-trip tests (compression → decompression = identical)
add_executable(test_roundtrip test_roundtrip.c)
target_link_libraries(test_roundtrip PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_roundtrip PRIVATE m)
endif()
add_test(NAME roundtrip COMMAND test_roundtrip)

# Targeted data type tests
add_executable(test_zeros test_zeros.c)
target_link_libraries(test_zeros PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_zeros PRIVATE m)
endif()
# CM-rANS tests
add_executable(test_cmrans test_cmrans.c)
target_link_libraries(test_cmrans PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_cmrans PRIVATE m)
endif()
add_test(NAME cmrans COMMAND test_cmrans)

# LZ77 engine tests
add_executable(test_lz test_lz.c
    ${CMAKE_SOURCE_DIR}/lib/lz/lz_compress.c
    ${CMAKE_SOURCE_DIR}/lib/lz/lz_decompress.c
    ${CMAKE_SOURCE_DIR}/lib/lz_fast/mcx_lz_fast_compress.c
    ${CMAKE_SOURCE_DIR}/lib/lz_fast/mcx_lz_fast_decompress.c
)
target_include_directories(test_lz PRIVATE ${CMAKE_SOURCE_DIR}/lib)
add_test(NAME lz77 COMMAND test_lz)

# FSE/tANS entropy tests
add_executable(test_fse test_fse.c
    ${CMAKE_SOURCE_DIR}/lib/entropy/fse.c
)
target_include_directories(test_fse PRIVATE ${CMAKE_SOURCE_DIR}/lib)
add_test(NAME fse COMMAND test_fse)

# FSE mini debug test
add_executable(test_fse_mini test_fse_mini.c
    ${CMAKE_SOURCE_DIR}/lib/entropy/fse.c
)
target_include_directories(test_fse_mini PRIVATE ${CMAKE_SOURCE_DIR}/lib)
add_test(NAME fse_mini COMMAND test_fse_mini)

# Multi-stream LZ77+FSE tests (Phase 3)
add_executable(test_lzfse test_lzfse.c
    ${CMAKE_SOURCE_DIR}/lib/lz/lz_compress.c
    ${CMAKE_SOURCE_DIR}/lib/lz/lz_decompress.c
    ${CMAKE_SOURCE_DIR}/lib/lz/lz_multistream.c
    ${CMAKE_SOURCE_DIR}/lib/entropy/fse.c
)
target_include_directories(test_lzfse PRIVATE ${CMAKE_SOURCE_DIR}/lib)
add_test(NAME lzfse COMMAND test_lzfse)

# BWT path validation (levels 10-22)
add_executable(test_bwt_levels test_bwt_levels.c)
target_link_libraries(test_bwt_levels PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_bwt_levels PRIVATE m)
endif()
add_test(NAME bwt_levels COMMAND test_bwt_levels)

# mcx-12 BWT round-trip (Canterbury regression)
add_executable(test_mcx12_bwt test_mcx12_bwt.c)
target_link_libraries(test_mcx12_bwt PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_mcx12_bwt PRIVATE m)
endif()
target_include_directories(test_mcx12_bwt PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME mcx12_bwt COMMAND test_mcx12_bwt
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Probe: kennedy.xls size sweep (L12 bug isolation)
add_executable(probe12 probe12.c)
target_link_libraries(probe12 PRIVATE maxcomp_static)
target_include_directories(probe12 PRIVATE ${CMAKE_SOURCE_DIR})

# Probe: real Canterbury files L12 (VERIFY FAIL investigation)
add_executable(probe12_real probe12_real.c)
target_link_libraries(probe12_real PRIVATE maxcomp_static)
target_include_directories(probe12_real PRIVATE ${CMAKE_SOURCE_DIR})

# Probe: kennedy.xls block-by-block decomp failure isolation
add_executable(probe12_blocks probe12_blocks.c)
target_link_libraries(probe12_blocks PRIVATE maxcomp_static)
target_include_directories(probe12_blocks PRIVATE ${CMAKE_SOURCE_DIR})

# Probe: kennedy.xls stage-by-stage genome analysis
add_executable(probe12_stage probe12_stage.c)
target_link_libraries(probe12_stage PRIVATE maxcomp_static)
target_include_directories(probe12_stage PRIVATE ${CMAKE_SOURCE_DIR})

# LZRC v2.0 roundtrip tests
add_executable(test_lzrc test_lzrc.c)
target_link_libraries(test_lzrc PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_lzrc PRIVATE m pthread)
endif()
if(OpenMP_C_FOUND)
    target_link_libraries(test_lzrc PRIVATE OpenMP::OpenMP_C)
endif()
add_test(NAME lzrc_roundtrip COMMAND test_lzrc)

# Phase J: Native speed harness for mcx_lz_fast
add_executable(bench_fast bench_fast.c
    ${CMAKE_SOURCE_DIR}/lib/lz_fast/mcx_lz_fast_compress.c
    ${CMAKE_SOURCE_DIR}/lib/lz_fast/mcx_lz_fast_decompress.c
    ${CMAKE_SOURCE_DIR}/lib/lz/lz_compress.c
    ${CMAKE_SOURCE_DIR}/lib/lz/lz_decompress.c
)
target_include_directories(bench_fast PRIVATE ${CMAKE_SOURCE_DIR}/lib ${CMAKE_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(bench_fast PRIVATE /O2 /arch:AVX2)
else()
    target_compile_options(bench_fast PRIVATE -O3 -march=native)
endif()


