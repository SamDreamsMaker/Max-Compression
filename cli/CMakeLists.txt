# ─── MaxCompression CLI ──────────────────────────────────────────────

add_executable(mcx main.c)
target_link_libraries(mcx PRIVATE maxcomp_static)

# Link math library on Unix
if(UNIX)
    target_link_libraries(mcx PRIVATE m)
endif()
