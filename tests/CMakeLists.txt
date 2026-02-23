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
