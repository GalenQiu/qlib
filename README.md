# qlib
A general purpose industrial C11 library: data structures, algorithms, network utilities and common tools.
Cross platform: Linux / Windows, MIT license.

## Features
- C11
- Abstract memory allocator, replaceable, with overflow checked array helpers
- Unified error codes with `ql_strerror`/`ql_ret_from_errno` and an optional
  error callback
- Opaque types for ABI stability
- Fully testable: minunit unit test, libFuzzer, benchmark
- Modular & optional components (network module can be disabled)
- Installable: CMake package config (`find_package(qlib)`) and pkg-config

## Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
./bin/qlib_test
```

Windows (MSVC, multi-config generator puts binaries in a per-config subfolder):
```
mkdir build; cd build
cmake ..
cmake --build . --config Debug
.\bin\Debug\qlib_test.exe
```

## Options

- `QLIB_BUILD_TESTS=ON` build unit tests
- `QLIB_BUILD_BENCH=OFF` build benchmark
- `QLIB_BUILD_FUZZ=OFF` build fuzz targets
- `QLIB_BUILD_NET=ON` build network module
- `QLIB_SHARED=OFF` build shared library instead of static
- `QLIB_WERROR=OFF` treat warnings as errors (recommended in CI)
- `QLIB_INSTALL=ON` generate install / package config rules
- `QLIB_SANITIZE=OFF` build with AddressSanitizer + UndefinedBehaviorSanitizer

## Using qlib

```c
#include <qlib/qlib.h>

int* values = ql_alloc_array(NULL, 16, sizeof(int)); /* NULL == default allocator */
if (values == NULL) {
    /* ql_strerror() turns any ql_ret_t into a message */
}
values = ql_realloc_array(NULL, values, 32, sizeof(int));
ql_dealloc(NULL, values);
```

`ql_alloc`, `ql_calloc`, `ql_alloc_array` and `ql_realloc_array` are all
overflow checked and report failures through the optional error handler:

```c
static void on_error(ql_ret_t code, const char* message, void* userdata) {
    fprintf(stderr, "qlib: %s (%s)\n", message, ql_strerror(code));
    (void)userdata;
}

ql_set_error_handler(on_error, NULL);
```

### As a CMake subdirectory
```cmake
add_subdirectory(third_party/qlib)
target_link_libraries(myapp PRIVATE qlib::qlib)
```

### From an installed package
```bash
cmake --install build --prefix /your/prefix
```
```cmake
find_package(qlib 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE qlib::qlib)
```

### With pkg-config
```bash
cc myapp.c $(pkg-config --cflags --libs qlib) -o myapp
```

> When linking the static library on Windows without CMake, define
> `QLIB_STATIC_DEFINE` so `QLIB_EXPORT` does not expand to `dllimport`.
