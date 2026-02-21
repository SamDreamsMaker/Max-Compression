# ─── MaxCompression Library ──────────────────────────────────────────

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
    lz/lz_decompress.c
)

# ─── Static library (always built) ──────────────────────────────────
add_library(maxcomp_static STATIC ${MCX_LIB_SOURCES})
target_include_directories(maxcomp_static
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(maxcomp_static PROPERTIES
    OUTPUT_NAME maxcomp
)
target_link_libraries(maxcomp_static PUBLIC OpenMP::OpenMP_C)

# Link math library on Unix
if(UNIX)
    target_link_libraries(maxcomp_static PRIVATE m)
endif()

# ─── Shared library (optional) ──────────────────────────────────────
if(BUILD_SHARED_LIBS)
    add_library(maxcomp_shared SHARED ${MCX_LIB_SOURCES})
    target_include_directories(maxcomp_shared
        PUBLIC  ${PROJECT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
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
install(TARGETS maxcomp_static
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/maxcomp
    DESTINATION include
)
