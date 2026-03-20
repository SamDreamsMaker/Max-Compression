# ─── MaxCompression Tests ────────────────────────────────────────────

# Core roundtrip tests (most important — validates lossless compression)
add_executable(test_roundtrip test_roundtrip.c)
target_link_libraries(test_roundtrip PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_roundtrip PRIVATE m)
endif()
add_test(NAME roundtrip COMMAND test_roundtrip)

# BWT path validation (levels 10-22)
add_executable(test_bwt_levels test_bwt_levels.c)
target_link_libraries(test_bwt_levels PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_bwt_levels PRIVATE m)
endif()
add_test(NAME bwt_levels COMMAND test_bwt_levels)

# LZRC v2.0 roundtrip tests (binary tree match finder + range coder)
add_executable(test_lzrc test_lzrc.c)
target_link_libraries(test_lzrc PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_lzrc PRIVATE m pthread)
endif()
if(OpenMP_C_FOUND)
    target_link_libraries(test_lzrc PRIVATE OpenMP::OpenMP_C)
endif()
add_test(NAME lzrc_roundtrip COMMAND test_lzrc)

# Comprehensive roundtrip tests (204 cases across all data types)
add_executable(test_comprehensive test_comprehensive.c)
target_link_libraries(test_comprehensive PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_comprehensive PRIVATE m)
endif()
add_test(NAME comprehensive COMMAND test_comprehensive)

# RLE2 (RUNA/RUNB) encoder/decoder tests
add_executable(test_rle2 test_rle2.c)
target_link_libraries(test_rle2 PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_rle2 PRIVATE m)
endif()
add_test(NAME rle2 COMMAND test_rle2)

# FSE/tANS entropy tests
add_executable(test_fse test_fse.c
    ${CMAKE_SOURCE_DIR}/lib/entropy/fse.c
)
target_include_directories(test_fse PRIVATE ${CMAKE_SOURCE_DIR}/lib)
add_test(NAME fse COMMAND test_fse)

# FSE mini test
add_executable(test_fse_mini test_fse_mini.c
    ${CMAKE_SOURCE_DIR}/lib/entropy/fse.c
)
target_include_directories(test_fse_mini PRIVATE ${CMAKE_SOURCE_DIR}/lib)
add_test(NAME fse_mini COMMAND test_fse_mini)

# Fuzz roundtrip test (random data, all levels)
add_executable(test_fuzz_roundtrip fuzz_roundtrip.c)
target_link_libraries(test_fuzz_roundtrip PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_fuzz_roundtrip PRIVATE m)
endif()
add_test(NAME fuzz_roundtrip COMMAND test_fuzz_roundtrip 1000 8192)
set_tests_properties(fuzz_roundtrip PROPERTIES TIMEOUT 120)

# Malformed input test (decompressor robustness)
add_executable(test_malformed test_malformed.c)
target_link_libraries(test_malformed PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_malformed PRIVATE m)
endif()
add_test(NAME malformed COMMAND test_malformed)

# Edge case tests (boundary conditions, extreme data patterns)
add_executable(test_edge_cases test_edge_cases.c)
target_link_libraries(test_edge_cases PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_edge_cases PRIVATE m)
endif()
add_test(NAME edge_cases COMMAND test_edge_cases)

# Stress tests (large blocks, extreme patterns, all levels)
add_executable(test_stress test_stress.c)
target_link_libraries(test_stress PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_stress PRIVATE m)
endif()
add_test(NAME stress COMMAND test_stress)
set_tests_properties(stress PROPERTIES TIMEOUT 120)

# LZRC fuzz test (L24 HC + L26 BT)
add_executable(fuzz_lzrc fuzz_lzrc.c)
target_link_libraries(fuzz_lzrc PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(fuzz_lzrc PRIVATE m)
endif()
add_test(NAME fuzz_lzrc COMMAND fuzz_lzrc 200 16384)
set_tests_properties(fuzz_lzrc PROPERTIES TIMEOUT 300)

# Compression ratio regression test (catches silent ratio changes)
add_executable(test_regression test_regression.c)
target_link_libraries(test_regression PRIVATE maxcomp_static)
target_compile_definitions(test_regression PRIVATE
    CORPUS_DIR="${CMAKE_SOURCE_DIR}/corpora")
if(UNIX)
    target_link_libraries(test_regression PRIVATE m)
endif()
add_test(NAME regression COMMAND test_regression)
set_tests_properties(regression PROPERTIES TIMEOUT 60)

# Streaming decompression test (decompress in small chunks, verify vs one-shot)
add_executable(test_streaming_decompress test_streaming_decompress.c)
target_link_libraries(test_streaming_decompress PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(test_streaming_decompress PRIVATE m)
endif()
add_test(NAME streaming_decompress COMMAND test_streaming_decompress)
set_tests_properties(streaming_decompress PROPERTIES TIMEOUT 120)

# Multi-file compress/decompress roundtrip (CLI integration test)
add_test(NAME multifile_roundtrip
    COMMAND ${CMAKE_SOURCE_DIR}/tests/test_multifile.sh ${CMAKE_BINARY_DIR}/bin/mcx)
set_tests_properties(multifile_roundtrip PROPERTIES TIMEOUT 30)

# CLI flags + pipe mode integration test
add_test(NAME cli_flags_pipe
    COMMAND ${CMAKE_SOURCE_DIR}/tests/test_cli_flags.sh ${CMAKE_BINARY_DIR}/bin/mcx)
set_tests_properties(cli_flags_pipe PROPERTIES TIMEOUT 60)
