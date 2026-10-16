add_executable(qlib_test
    minunit.c
    test_main.c
    test_common.c
)

# qlib_warnings is an INTERFACE target defined at the top level: the test code
# is held to the same warning standard as the library itself.
target_link_libraries(qlib_test PRIVATE qlib::qlib qlib_warnings)

# Keeps the documented ./bin/qlib_test path valid. Multi-config generators
# (MSVC, Xcode) append the configuration name, e.g. bin/Debug/qlib_test.exe.
set_target_properties(qlib_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
)

add_test(NAME qlib_common_test COMMAND qlib_test)
