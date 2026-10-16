# Benchmarks.
#
# Only built when QLIB_BUILD_BENCH=ON. Nothing here is installed: the benchmark
# is a developer tool, not part of the library.

set(QLIB_BENCH_SRC
    bench_main.c
    bench_vector.c
    bench_mem.c
)

# bench_net.c only exists when the optional network module is built; the suite
# registration in bench_main.c is guarded by the same macro.
if(QLIB_BUILD_NET)
    list(APPEND QLIB_BENCH_SRC bench_net.c)
endif()

add_executable(qlib_bench ${QLIB_BENCH_SRC})

target_link_libraries(qlib_bench PRIVATE qlib::qlib qlib_warnings)

set_target_properties(qlib_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
)

# A tiny iteration count, so ctest proves the benchmark still builds, runs and
# passes its own correctness checks. The timings from this run are meaningless
# and the smoke test does not assert on them.
add_test(NAME qlib_bench_smoke COMMAND qlib_bench --quick)
