# ─── MaxCompression Benchmarks ───────────────────────────────────────

add_executable(bench_compress bench_compress.c)
target_link_libraries(bench_compress PRIVATE maxcomp_static)
if(UNIX)
    target_link_libraries(bench_compress PRIVATE m)
endif()
