# Getting started

## Requirements

- A C11 compiler: GCC, Clang, AppleClang or MSVC
- CMake 3.16 or newer

Nothing else is required to build the library or its tests. Doxygen is only
needed if you want to generate the API reference, and Clang only if you want to
build the fuzz targets.

## Build and test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The test binary is also usable directly, which is handy while iterating:

```bash
./build/bin/qlib_test
```

It prints a single summary line and exits non-zero when anything failed:

```
39 tests run, 0 failures
```

On Windows with a multi-config generator (Visual Studio, Xcode) the binaries
land in a per-configuration subdirectory and the configuration must be named
for every build and test command:

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\bin\Debug\qlib_test.exe
```

## Build options

Every option is a normal CMake cache variable, so pass it with `-D` at
configure time.

| Option | Default | Effect |
|---|---|---|
| `QLIB_BUILD_TESTS` | `ON` at top level, `OFF` otherwise | Build the unit tests |
| `QLIB_BUILD_FUZZ` | `OFF` | Build libFuzzer targets. Requires Clang; other compilers produce a warning |
| `QLIB_BUILD_BENCH` | `OFF` | Not implemented yet; enabling it prints a warning |
| `QLIB_BUILD_NET` | `OFF` | Not implemented yet; enabling it prints a warning |
| `QLIB_SHARED` | `OFF` | Build a shared library instead of a static one |
| `QLIB_WERROR` | `OFF` | Treat compiler warnings as errors. Recommended in CI |
| `QLIB_INSTALL` | `ON` | Generate the install and package-config rules |
| `QLIB_SANITIZE` | `OFF` | Build with AddressSanitizer and UndefinedBehaviorSanitizer |
| `QLIB_BUILD_DOCS` | `OFF` | Generate the API reference with Doxygen |

`QLIB_BUILD_TESTS` defaults to `ON` only when qlib is the top level project, so
a project that pulls qlib in with `add_subdirectory()` gets the library without
a second test suite.

## Building with sanitizers

This is the first thing to try when something looks like a memory bug, and the
default way to run the tests locally:

```bash
cmake -S . -B build-asan -DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

`QLIB_SANITIZE=ON` adds `-fsanitize=address,undefined -fno-omit-frame-pointer
-g`. Under MSVC you get AddressSanitizer only, because MSVC has no
UndefinedBehaviorSanitizer.

## Fuzzing

```bash
CC=clang cmake -S . -B build-fuzz -DQLIB_BUILD_FUZZ=ON
cmake --build build-fuzz
./build/bin/fuzz_vector -max_total_time=60
```

`-fsanitize=fuzzer` is a Clang feature. With GCC or MSVC the option warns and
no fuzz targets are produced.

## Generating the API reference

```bash
cmake -S . -B build -DQLIB_BUILD_DOCS=ON
cmake --build build --target docs
```

Open `docs/html/index.html` afterwards. See [the docs index](../README.md) for
the manual Doxygen route.

## A first program

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_vector_t* vec = NULL;
    int i;

    /* NULL means "use the default allocator". */
    if (ql_vector_create(NULL, sizeof(int), &vec) != QL_OK) {
        return 1;
    }

    for (i = 0; i < 10; i++) {
        if (ql_vector_push_back(vec, &i) != QL_OK) {
            ql_vector_destroy(vec);
            return 1;
        }
    }

    printf("%zu elements, capacity %zu\n", ql_vector_size(vec), ql_vector_capacity(vec));

    ql_vector_destroy(vec);
    return 0;
}
```

`<qlib/qlib.h>` is an umbrella header that pulls in every module. Including the
individual headers, for example `<qlib/ql_vector.h>`, is equally fine and
compiles faster.

## Next steps

- [Integration](integration.md) — how to depend on qlib from your own build
- [Error handling](errors.md) — what to do with the return codes
- [Memory](memory.md) — allocators and the allocation helpers
- [Vector](vector.md) — using the growable array
