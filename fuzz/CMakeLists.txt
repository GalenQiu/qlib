# libFuzzer targets. -fsanitize=fuzzer is a Clang feature; with any other
# compiler there is nothing we can build, so say so instead of silently
# producing nothing.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")

    set(QLIB_FUZZ_TARGETS
        fuzz_vector
        fuzz_mem
    )

    foreach(target IN LISTS QLIB_FUZZ_TARGETS)
        add_executable(${target} ${target}.c)

        target_link_libraries(${target} PRIVATE qlib::qlib qlib_warnings)

        target_compile_options(${target} PRIVATE
            -fsanitize=fuzzer,address,undefined
            -fno-omit-frame-pointer
            -g
        )
        target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)

        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}"
        )

        # A short deterministic smoke run, so ctest exercises the target too.
        add_test(NAME ${target}_smoke
            COMMAND ${target} -runs=2000 -max_len=128
        )
    endforeach()

else()

    message(WARNING
        "qlib: QLIB_BUILD_FUZZ=ON requires Clang for -fsanitize=fuzzer; "
        "no fuzz targets will be built with ${CMAKE_C_COMPILER_ID}."
    )

endif()
