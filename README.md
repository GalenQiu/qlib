# qlib

[![CI](https://github.com/GalenQiu/qlib/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/GalenQiu/qlib/actions/workflows/ci.yml)

A general purpose industrial C11 library: data structures, algorithms, network utilities and common tools.
Cross platform: Linux / macOS / Windows, MIT license.

## Features
- C11
- `ql_vector`: an opaque, growable, contiguous array
- `ql_str`: a growable, always NUL terminated byte string
- `ql_hash`: a hash table of fixed size keys to fixed size values
- `ql_list`: a doubly linked list with stable node handles
- `ql_thread`: threads, mutexes and condition variables, POSIX and Windows
- `ql_log`: a logger with levels, plain/raw/JSON output and rotating files
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
| `<qlib/ql_str.h>` | `ql_str_t`, a growable, always NUL terminated byte string |
| `<qlib/ql_hash.h>` | `ql_hash_t`, a hash table of fixed size keys to fixed size values |
| `<qlib/ql_list.h>` | `ql_list_t`, a doubly linked list with stable node handles |
| `<qlib/ql_thread.h>` | threads, mutexes and condition variables (optional module) |
| `<qlib/ql_log.h>` | logger: levels, formats, console and rotating files (optional module) |
| `<qlib/ql_net.h>` | IPv4 address parsing, formatting and TCP sockets (optional module) |
| `<qlib/qlib.h>` | umbrella header pulling in all of the above |

## Documentation

The full documentation lives in [`docs/`](docs/index.md), in English with a
[Simplified Chinese translation](docs/zh/index.md):

- **User guide** — [getting started](docs/user/getting-started.md),
  [integration](docs/user/integration.md),
  [error handling](docs/user/errors.md),
  [memory](docs/user/memory.md),
  [vector](docs/user/vector.md),
  [strings](docs/user/string.md),
  [hash table](docs/user/hash.md),
  [linked list](docs/user/list.md),
  [threads](docs/user/thread.md),
  [logging](docs/user/log.md),
  [network addresses](docs/user/net.md)
- **Technical docs** — [architecture](docs/technical/architecture.md),
  [design decisions](docs/technical/design-decisions.md),
  [ABI and versioning](docs/technical/abi-and-versioning.md),
  [portability](docs/technical/portability.md),
  [build system](docs/technical/build-system.md),
  [testing](docs/technical/testing.md)

The API reference is generated with Doxygen:

```bash
cmake -S . -B build -DQLIB_BUILD_DOCS=ON
cmake --build build --target docs      # writes docs/html/index.html
```

Release history, and what is still missing, is in [`CHANGELOG.md`](CHANGELOG.md).
Contributors should read [`CONTRIBUTING.md`](CONTRIBUTING.md) and, if they read
Chinese, [the development workflow guide](docs/zh/development-workflow.md).

## Build
```bash
mkdir build && cd build
cmake ..
cmake --build .
./bin/qlib_test
```

Or run the whole local verification — build, tests, sanitizers, fuzzing, static
analysis and the documentation checks — in one command:

```bash
./ci/check.sh            # everything
./ci/check.sh --quick    # skip the sanitizer build and fuzzing
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
- `QLIB_BUILD_BENCH=OFF` build the micro-benchmark suite (`bin/qlib_bench`)
- `QLIB_BUILD_FUZZ=OFF` build libFuzzer targets; requires Clang, and with any
  other compiler the option warns instead of silently doing nothing
- `QLIB_BUILD_THREADS=OFF` build the optional threading module (POSIX and Windows)
- `QLIB_BUILD_LOG=OFF` build the optional logging module (requires `QLIB_BUILD_THREADS`)
- `QLIB_BUILD_NET=ON` build the optional network module (address handling and TCP sockets)
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
