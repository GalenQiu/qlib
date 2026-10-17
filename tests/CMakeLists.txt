# ---------------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------------

# qlib_warnings is an INTERFACE target defined at the top level: the test code
# is held to the same warning standard as the library itself.

# test_net.c only exists when the optional network module is built, and the
# matching MU_RUN_SUITE call in test_main.c is guarded by the same macro.
set(QLIB_TEST_SRC
    minunit.c
    test_main.c
    test_common.c
    test_hash.c
    test_list.c
    test_mem.c
    test_str.c
    test_vector.c
)

if(QLIB_BUILD_NET)
    list(APPEND QLIB_TEST_SRC test_net.c)
endif()

if(QLIB_BUILD_THREADS)
    list(APPEND QLIB_TEST_SRC test_thread.c)
endif()

if(QLIB_BUILD_LOG)
    list(APPEND QLIB_TEST_SRC test_log.c)
endif()

add_executable(qlib_test ${QLIB_TEST_SRC})

target_link_libraries(qlib_test PRIVATE qlib::qlib qlib_warnings)

# Keeps the documented ./bin/qlib_test path valid. Multi-config generators
# (MSVC, Xcode) append the configuration name, e.g. bin/Debug/qlib_test.exe.
set_target_properties(qlib_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
)

add_test(NAME qlib_common_test COMMAND qlib_test)

# ---------------------------------------------------------------------------
# Test harness self check
#
# A probe that is *supposed* to fail, so that "a failing assertion must fail
# the run" is verified on every CI run instead of being taken on trust. See
# selftest/expect_failure.cmake for the assertions.
# ---------------------------------------------------------------------------

add_executable(qlib_harness_selftest
    minunit.c
    selftest/harness_main.c
    selftest/harness_suite.c
)

# Does not link qlib: the probe only exercises the test harness.
target_link_libraries(qlib_harness_selftest PRIVATE qlib_warnings)

target_include_directories(qlib_harness_selftest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/selftest"
)

set_target_properties(qlib_harness_selftest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
)

add_test(NAME qlib_harness_selftest
    COMMAND ${CMAKE_COMMAND}
        "-DPROBE=$<TARGET_FILE:qlib_harness_selftest>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/selftest/expect_failure.cmake"
)
