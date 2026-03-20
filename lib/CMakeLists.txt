# ─── MaxCompression Library ──────────────────────────────────────────
include(GNUInstallDirs)

# Collect all source files
set(MCX_LIB_SOURCES
    core.c
    analyzer/analyzer.c
    preprocess/bwt.c
    preprocess/delta.c
    preprocess/rle.c
    model/context.c
    entropy/huffman.c
    entropy/ans.c
    entropy/fse.c
    optimizer/genetic.c
    lz/lz_compress.c
    lz/bt_match.c
lz/hc_match.c
    lz/lzrc.c
    lz/lz_decompress.c
    lz/lz_multistream.c
    lz/lz24.c
    lz_fast/mcx_lz_fast_compress.c
    lz_fast/mcx_lz_fast_decompress.c
    babel/babel_transform.c
    babel/babel_stride.c
    preprocess/rle2.c
preprocess/e8e9.c
preprocess/lzp.c
preprocess/nibble_split.c
    # babel/babel_dict.c  # Moved to experimental/
    entropy/adaptive_ac.c
    entropy/multi_rans.c
    entropy/range_coder.c
    cm/cm.c
    external/divsufsort/divsufsort.c
    external/divsufsort/sssort.c
    external/divsufsort/trsort.c
    external/divsufsort/utils.c
)

# ─── Static library (always built) ──────────────────────────────────
add_library(maxcomp_static STATIC ${MCX_LIB_SOURCES})
target_include_directories(maxcomp_static
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/divsufsort
)
# Enable divsufsort for faster BWT
target_compile_definitions(maxcomp_static PRIVATE MCX_USE_DIVSUFSORT=1)
# divsufsort needs HAVE_CONFIG_H to load its config
set_source_files_properties(
    external/divsufsort/divsufsort.c
    external/divsufsort/sssort.c
    external/divsufsort/trsort.c
    external/divsufsort/utils.c
    PROPERTIES COMPILE_DEFINITIONS "HAVE_CONFIG_H=1"
)
set_target_properties(maxcomp_static PROPERTIES
    OUTPUT_NAME maxcomp
)
if(OpenMP_C_FOUND)
    target_link_libraries(maxcomp_static PUBLIC OpenMP::OpenMP_C)
endif()

# Link math library on Unix
if(UNIX)
    target_link_libraries(maxcomp_static PRIVATE m)
endif()

# ─── Shared library (optional) ──────────────────────────────────────
if(BUILD_SHARED_LIBS)
    add_library(maxcomp_shared SHARED ${MCX_LIB_SOURCES})
    target_include_directories(maxcomp_shared
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/divsufsort
    )
    target_compile_definitions(maxcomp_shared PRIVATE MCX_USE_DIVSUFSORT=1)
    target_compile_definitions(maxcomp_shared
        PRIVATE MCX_BUILDING=1
        PUBLIC  MCX_SHARED=1
    )
    set_target_properties(maxcomp_shared PROPERTIES
        OUTPUT_NAME maxcomp
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    if(UNIX)
        target_link_libraries(maxcomp_shared PRIVATE m)
    endif()
endif()

# ─── Installation Directives (Required for Cargo / cmake-rs) ────────
# Note: install(EXPORT) is handled in top-level CMakeLists.txt
