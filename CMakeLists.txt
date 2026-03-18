cmake_minimum_required(VERSION 3.15)

project(maxcomp
    VERSION 1.9.3
    DESCRIPTION "MaxCompression — Revolutionary file compression library"
    LANGUAGES C
)

# ─── Prevent in-source builds ────────────────────────────────────────
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -S . -B build")
endif()

# ─── C Standard ──────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ─── OpenMP ──────────────────────────────────────────────────────────
find_package(OpenMP REQUIRED)

# ─── Build Options ───────────────────────────────────────────────────
option(BUILD_SHARED_LIBS   "Build shared library (DLL/so/dylib)"   ON)
option(MCX_BUILD_CLI       "Build the mcx command-line tool"       ON)
option(MCX_BUILD_TESTS     "Build unit tests"                      ON)
option(MCX_BUILD_BENCHMARKS "Build benchmarks"                     ON)

# ─── Output directories ─────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ─── Library (always built) ──────────────────────────────────────────
add_subdirectory(lib)

# ─── CLI tool ────────────────────────────────────────────────────────
if(MCX_BUILD_CLI)
    add_subdirectory(cli)
endif()

# ─── Tests ───────────────────────────────────────────────────────────
if(MCX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── Benchmarks ──────────────────────────────────────────────────────
if(MCX_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# ─── Summary ─────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════╗")
message(STATUS "║        MaxCompression v${PROJECT_VERSION}               ║")
message(STATUS "╠══════════════════════════════════════════════╣")
message(STATUS "║  Shared library:  ${BUILD_SHARED_LIBS}")
message(STATUS "║  CLI tool:        ${MCX_BUILD_CLI}")
message(STATUS "║  Tests:           ${MCX_BUILD_TESTS}")
message(STATUS "║  Benchmarks:      ${MCX_BUILD_BENCHMARKS}")
message(STATUS "║  C Compiler:      ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "║  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "╚══════════════════════════════════════════════╝")
message(STATUS "")
# ─── Install targets ─────────────────────────────────────────────────
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/maxcomp DESTINATION include)
install(TARGETS maxcomp_static ARCHIVE DESTINATION lib)
if(BUILD_SHARED_LIBS)
    install(TARGETS maxcomp_shared LIBRARY DESTINATION lib)
endif()
if(MCX_BUILD_CLI)
    install(TARGETS mcx RUNTIME DESTINATION bin)
endif()

# ─── pkg-config ──────────────────────────────────────────────────────
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/maxcomp.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/maxcomp.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/maxcomp.pc DESTINATION lib/pkgconfig)