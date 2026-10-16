# libFuzzer targets. -fsanitize=fuzzer is a Clang feature; with any other
# compiler there is nothing we can build, so say so instead of silently
# producing nothing.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")

    add_executable(fuzz_vector fuzz_vector.c)

    target_link_libraries(fuzz_vector PRIVATE qlib::qlib qlib_warnings)

    target_compile_options(fuzz_vector PRIVATE
        -fsanitize=fuzzer,address,undefined
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(fuzz_vector PRIVATE -fsanitize=fuzzer,address,undefined)

    set_target_properties(fuzz_vector PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
    )

    # A short deterministic smoke run, so ctest exercises the target too.
    add_test(NAME fuzz_vector_smoke
        COMMAND fuzz_vector -runs=2000 -max_len=128
    )

else()

    message(WARNING
        "qlib: QLIB_BUILD_FUZZ=ON requires Clang for -fsanitize=fuzzer; "
        "no fuzz targets will be built with ${CMAKE_C_COMPILER_ID}."
    )

endif()
