# ─── MaxCompression CLI ──────────────────────────────────────────────

add_executable(mcx main.c)
if(MSVC)
    # CLI uses internal library symbols (extern globals, internal functions)
    # MSVC only pulls referenced .obj from static libs, so force whole-archive.
    target_link_options(mcx PRIVATE /WHOLEARCHIVE:maxcomp)
endif()
target_link_libraries(mcx PRIVATE maxcomp_static)
# Pass library feature flags for `mcx version --build` display
target_compile_definitions(mcx PRIVATE MCX_USE_DIVSUFSORT=1)

# Link math library on Unix
if(UNIX)
    target_link_libraries(mcx PRIVATE m)
endif()
