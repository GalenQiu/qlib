# ctest driver for the test-harness self check.
#
# Runs the probe executable and asserts that it
#   (a) exits with a non-zero status, and
#   (b) reports exactly the single failure it was built to produce.
#
# Checking the reported counts as well as the exit status matters: ctest's
# WILL_FAIL property inverts on *any* non-zero exit, so a probe that crashed
# with a segfault would be recorded as a pass.

if(NOT DEFINED PROBE)
    message(FATAL_ERROR "expect_failure.cmake: PROBE is not set")
endif()

execute_process(
    COMMAND "${PROBE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_stdout
    ERROR_VARIABLE probe_stderr
)

set(probe_output "${probe_stdout}${probe_stderr}")

if(probe_result EQUAL 0)
    message(FATAL_ERROR
        "test harness self check FAILED: a failing assertion exited with success.\n"
        "The test counters are most likely no longer shared across translation units.\n"
        "Probe output:\n${probe_output}"
    )
endif()

if(NOT probe_output MATCHES "1 tests run, 1 failures")
    message(FATAL_ERROR
        "test harness self check FAILED: expected '1 tests run, 1 failures' in the report.\n"
        "Exit status was ${probe_result}.\n"
        "Probe output:\n${probe_output}"
    )
endif()

message(STATUS "test harness self check passed (exit=${probe_result}, report matched)")
