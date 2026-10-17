# A terminal dashboard built on the whole library: threads, a hash table, a
# list, a vector, strings, the logger and the network address code, all in one
# program. It is the counterweight to a README full of snippets -- if the
# modules do not compose, this is where it shows.
add_executable(qlib_workbench workbench.c)
target_link_libraries(qlib_workbench PRIVATE qlib::qlib qlib_warnings)

# Keeps the documented ./bin/qlib_workbench path valid.
set_target_properties(qlib_workbench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(QLIB_BUILD_TESTS)
    # --frames makes the dashboard non-interactive, which is the only reason a
    # TUI can be smoke-tested at all. Two frames is enough to prove that a frame
    # renders, the workers run and the shutdown path joins them.
    add_test(NAME qlib_workbench_smoke COMMAND qlib_workbench --frames 2 --workers 2 --fps 20)
endif()
