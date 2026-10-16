# qlib
A general purpose industrial C11 library: data structures, algorithms, network utilities and common tools.
Cross platform: Linux / Windows, MIT license.

## Features
- C11
- `ql_vector`: an opaque, growable, contiguous array
- Abstract memory allocator, replaceable, with overflow checked array helpers
- Unified error codes with `ql_strerror`/`ql_ret_from_errno` and an optional
  error callback
- Opaque types for ABI stability
- Fully testable: minunit unit test, libFuzzer, benchmark
- Modular & optional components (network module can be disabled)
- Installable: CMake package config (`find_package(qlib)`) and pkg-config

## Modules

| Header | Contents |
|---|---|
| `<qlib/ql_common.h>` | version, error codes, `ql_strerror`, error handler |
| `<qlib/ql_mem.h>` | `ql_allocator_t` and the overflow checked allocation helpers |
| `<qlib/ql_vector.h>` | `ql_vector_t`, a growable array of fixed size elements |
| `<qlib/qlib.h>` | umbrella header pulling in all of the above |

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

- `QLIB_BUILD_TESTS=ON` build unit tests (ON by default only when qlib is the
  top level project, so `add_subdirectory()` consumers get just the library)
- `QLIB_BUILD_BENCH=OFF` build benchmark (not implemented yet)
- `QLIB_BUILD_FUZZ=OFF` build libFuzzer targets; requires Clang, and with any
  other compiler the option warns instead of silently doing nothing
- `QLIB_BUILD_NET=OFF` build network module (not implemented yet)
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

### A growable array

```c
#include <qlib/qlib.h>

ql_vector_t* v = NULL;
int i;

if (ql_vector_create(NULL, sizeof(int), &v) != QL_OK) {
    return -1;                       /* allocator is copied in; NULL means default */
}
for (i = 0; i < 100; i++) {
    if (ql_vector_push_back(v, &i) != QL_OK) {
        ql_vector_destroy(v);
        return -1;
    }
}

int* third = ql_vector_at(v, 3);     /* NULL when out of range */
if (third != NULL) {
    printf("%d\n", *third);
}
ql_vector_destroy(v);
```

Elements are copied bytewise, so element types must be trivially copyable.
Pointers returned by `ql_vector_at`/`ql_vector_data` are invalidated by any
operation that can reallocate, so re-fetch them after mutating.

### Fuzzing

```bash
CC=clang cmake -S . -B build -DQLIB_BUILD_FUZZ=ON
cmake --build build
./bin/fuzz_vector -runs=1000000
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
