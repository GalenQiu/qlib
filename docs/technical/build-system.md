# Build System

## Project layout

| Path | Contents |
|---|---|
| [`CMakeLists.txt`](../../CMakeLists.txt) | Root project: options, sanitizers, warning target, library target, subdirectories, install/export rules |
| [`tests/CMakeLists.txt`](../../tests/CMakeLists.txt) | The `qlib_test` executable and the `qlib_harness_selftest` probe |
| [`fuzz/CMakeLists.txt`](../../fuzz/CMakeLists.txt) | The `fuzz_vector` and `fuzz_mem` libFuzzer targets |
| [`bench/CMakeLists.txt`](../../bench/CMakeLists.txt) | The `qlib_bench` micro-benchmark suite (network cases included when the module is built) |
| [`cmake/qlibConfig.cmake.in`](../../cmake/qlibConfig.cmake.in) | Package config template for `find_package(qlib)` |
| [`cmake/qlib.pc.in`](../../cmake/qlib.pc.in) | pkg-config template |
| [`cmake/cmake_uninstall.cmake.in`](../../cmake/cmake_uninstall.cmake.in) | Script run by the `uninstall` target |
| [`docs/Doxyfile.in`](../../docs/Doxyfile.in) | Doxygen configuration template for the `docs` target |

`cmake_minimum_required(VERSION 3.16)`. The root file includes `GNUInstallDirs`, `CMakePackageConfigHelpers` and `GenerateExportHeader`.

## Target graph

```text
qlib_warnings (INTERFACE)          build-only helper, never installed
      |  PRIVATE $<BUILD_INTERFACE:...>
      v
qlib (STATIC or SHARED)  <---- ALIAS ---->  qlib::qlib
      ^                                        ^
      |  PRIVATE qlib::qlib                     |  PRIVATE qlib::qlib
      |                                        |
  qlib_test                          fuzz_vector        (Clang only)
  qlib_harness_selftest  (links qlib_warnings only)
```

| Target | Type | Defined in | Links | Output |
|---|---|---|---|---|
| `qlib` | `STATIC` or `SHARED` per `QLIB_SHARED` | root | `qlib_warnings` (private, build interface) | `${PROJECT_BINARY_DIR}/lib` |
| `qlib::qlib` | `ALIAS` | root | — | — |
| `qlib_warnings` | `INTERFACE` | root | — | — |
| `qlib_test` | executable | `tests/` | `qlib::qlib`, `qlib_warnings` | `${PROJECT_BINARY_DIR}/bin` |
| `qlib_harness_selftest` | executable | `tests/` | `qlib_warnings` only | `${PROJECT_BINARY_DIR}/bin` |
| `fuzz_vector`, `fuzz_mem` | executables | `fuzz/` | `qlib::qlib`, `qlib_warnings` | `${PROJECT_BINARY_DIR}/bin` |
| `qlib_bench` | executable | `bench/` | `qlib::qlib`, `qlib_warnings` | `${PROJECT_BINARY_DIR}/bin` |
| `docs` | custom target | root, when `QLIB_BUILD_DOCS=ON` | — | `docs/html` |

`docs` is a custom target, not linked into anything: it is built explicitly with `cmake --build build --target docs`. It requires Doxygen, and optionally uses Graphviz when the `dot` component is found.

Consumers should always link `qlib::qlib`, never `qlib`. The output directories are set so that the documented paths hold: `libqlib.a` / `libqlib.so` under `build/lib`, and every executable under `build/bin`. On multi-config generators (MSVC, Xcode) CMake appends the configuration, so the test binary is `build/bin/Debug/qlib_test.exe`.

Registered tests:

| `add_test` name | Command | Registered in |
|---|---|---|
| `qlib_common_test` | `qlib_test` | `tests/CMakeLists.txt` |
| `qlib_harness_selftest` | `${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:qlib_harness_selftest> -P tests/selftest/expect_failure.cmake` | `tests/CMakeLists.txt` |
| `fuzz_vector_smoke`, `fuzz_mem_smoke` | `<target> -runs=2000 -max_len=128` | `fuzz/CMakeLists.txt`, one per target, only when they exist |
| `qlib_bench_smoke` | `qlib_bench --quick` | `bench/CMakeLists.txt`, only when the target exists |

## Cache options

| Option | Default | Effect |
|---|---|---|
| `QLIB_BUILD_TESTS` | `ON` if top level, else `OFF` | Adds the `tests/` subdirectory and calls `enable_testing()` |
| `QLIB_BUILD_BENCH` | `OFF` | Adds the `bench/` subdirectory, building the `qlib_bench` executable |
| `QLIB_BUILD_FUZZ` | `OFF` | Adds the `fuzz/` subdirectory. Requires Clang; otherwise a `WARNING` |
| `QLIB_BUILD_NET` | `ON` | Adds `src/ql_net.c` to the library, installs `ql_net.h`, and defines `QLIB_HAS_NET` for consumers |
| `QLIB_SHARED` | `OFF` | `add_library(qlib SHARED ...)` instead of `STATIC` |
| `QLIB_WERROR` | `OFF` | Adds `-Werror` (GCC/Clang) or `/WX` (MSVC) to `qlib_warnings` |
| `QLIB_INSTALL` | `ON` | Enables all install/export/package-config/pkg-config/uninstall rules |
| `QLIB_SANITIZE` | `OFF` | Adds ASAN+UBSAN flags with directory scope |
| `QLIB_BUILD_DOCS` | `OFF` | Requires Doxygen; configures `docs/Doxyfile.in` and adds the `docs` custom target |
| `QLIB_INSTALL_CMAKEDIR` | `${CMAKE_INSTALL_LIBDIR}/cmake/qlib` | `CACHE STRING`; destination of the CMake package files. Defined inside the `QLIB_INSTALL` block |

Plain (non-cache) variables set by the root file:

| Variable | Value | Purpose |
|---|---|---|
| `CMAKE_C_STANDARD` / `CMAKE_C_STANDARD_REQUIRED` / `CMAKE_C_EXTENSIONS` | `11` / `ON` / `OFF` | Directory defaults for the target properties, pinning every first-party target to `-std=c11` with no extensions |
| `QLIB_C_STANDARD` | `c_std_11` | Passed to `target_compile_features(qlib PUBLIC ...)` so consumers inherit a C11 floor |
| `QLIB_TESTS_DEFAULT` | `ON`/`OFF` | Computed default fed to the `QLIB_BUILD_TESTS` option |
| `QLIB_SRC` | `src/ql_common.c`, `src/ql_mem.c`, `src/ql_vector.c` | Source list for the library target |
| `DOXYGEN_HAVE_DOT` | `YES`/`NO` | Substituted into `Doxyfile.in` as `HAVE_DOT` |

The C standard is pinned through both mechanisms on purpose. `target_compile_features()` alone expresses a floor: a compiler whose default dialect already satisfies `c_std_11` receives no `-std=` flag at all and builds as, say, C23. `C_STANDARD` plus `C_STANDARD_REQUIRED` and `C_EXTENSIONS OFF` is what actually emits `-std=c11`. See [portability](portability.md).

The top-level test is done with a source-directory comparison rather than `PROJECT_IS_TOP_LEVEL`, which would require CMake 3.21:

```cmake
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(QLIB_TESTS_DEFAULT ON)
else()
    set(QLIB_TESTS_DEFAULT OFF)
endif()
```

This means an `add_subdirectory()` consumer builds only the library unless it explicitly asks for tests.

## Build and install interface

Public include directories use the generator-expression split:

```cmake
target_include_directories(qlib PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
```

Two directories appear on the build side because `include/qlib/qlib_export.h` is generated into the build tree while the rest of the headers are in the source tree. On the install side a single relative path suffices, since all five headers end up in the same directory. `$<INSTALL_INTERFACE:...>` is what prevents the exporting machine's absolute source path from appearing in `qlibTargets.cmake`.

The same reasoning applies to the warning flags:

```cmake
target_link_libraries(qlib PRIVATE $<BUILD_INTERFACE:qlib_warnings>)
```

`qlib_warnings` is a build-only tool: it carries `-Wall -Wextra ...` for this project and must not become a requirement on downstream consumers. Without the `$<BUILD_INTERFACE:>` wrapper, CMake rejects the configure with *"target "qlib_warnings" that is not in any export set"*, because `install(EXPORT qlibTargets ...)` cannot describe a dependency the package does not ship. Wrapping it keeps the helper out of the exported target's interface.

For the same reason `install(TARGETS qlib ...)` has no `INCLUDES DESTINATION` clause: `target_include_directories()` already contributes an `$<INSTALL_INTERFACE:...>` entry, and specifying both duplicates the path in the generated `INTERFACE_INCLUDE_DIRECTORIES`. Verified: the installed `qlibTargets.cmake` contains exactly one entry, `"${_IMPORT_PREFIX}/include"`.

## Export header

```cmake
generate_export_header(qlib
    BASE_NAME QLIB
    EXPORT_MACRO_NAME QLIB_EXPORT
    EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/qlib/qlib_export.h"
)
```

The generated file lands in the build tree, is installed alongside the other headers, and is what defines `QLIB_EXPORT` and `QLIB_NO_EXPORT`. For a static build the target also adds `QLIB_STATIC_DEFINE` as a `PUBLIC` definition. See [ABI and versioning](abi-and-versioning.md) for what the macro expands to per platform.

## Install, export and package config

Everything below is inside `if(QLIB_INSTALL)`.

| Rule | Destination |
|---|---|
| `install(TARGETS qlib EXPORT qlibTargets ...)` | `lib/` for archive and library, `bin/` for runtime |
| `install(DIRECTORY include/qlib ... FILES_MATCHING PATTERN "*.h")` | `include/qlib/` |
| `install(FILES <build>/include/qlib/qlib_export.h ...)` | `include/qlib/` |
| `install(EXPORT qlibTargets FILE qlibTargets.cmake NAMESPACE qlib:: ...)` | `lib/cmake/qlib/` |
| `write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)` | `lib/cmake/qlib/qlibConfigVersion.cmake` |
| `configure_package_config_file(cmake/qlibConfig.cmake.in ...)` | `lib/cmake/qlib/qlibConfig.cmake` |
| `configure_file(cmake/qlib.pc.in ...)` then `install(FILES ...)` | `lib/pkgconfig/qlib.pc` |

Verified installed tree from a default static build:

```text
include/qlib/ql_common.h
include/qlib/qlib_export.h
include/qlib/qlib.h
include/qlib/ql_mem.h
include/qlib/ql_vector.h
lib/cmake/qlib/qlibConfig.cmake
lib/cmake/qlib/qlibConfigVersion.cmake
lib/cmake/qlib/qlibTargets.cmake
lib/cmake/qlib/qlibTargets-noconfig.cmake
lib/libqlib.a
lib/pkgconfig/qlib.pc
```

The pkg-config file is generated with a relocatable prefix rather than the configure-time `CMAKE_INSTALL_PREFIX`:

```text
prefix=${pcfiledir}/../..
```

`cmake --install --prefix <other>` and `DESTDIR` staging both change where the file is written without updating the value baked in at configure time, so a literal prefix would point at a directory the package was never installed to. `${pcfiledir}` is resolved by pkg-config from the file's own location instead.

## Uninstall target

`cmake/cmake_uninstall.cmake.in` is configured into the build tree and driven by a custom target:

```bash
cmake --build build --target uninstall
```

It reads `install_manifest.txt` from the build directory, fails with `FATAL_ERROR` if the manifest is missing, and removes each recorded path. It only removes files that a `cmake --install` from that same build directory recorded.

## Sanitizers

`QLIB_SANITIZE` uses directory scope rather than a target interface:

```cmake
if(QLIB_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
    message(STATUS "qlib: sanitizers enabled (QLIB_SANITIZE=ON)")
endif()
```

Two reasons for the placement and the mechanism:

- The block sits **before** `add_library(qlib ...)`. Directory properties are captured by targets created afterwards, and are inherited by subdirectories added later, so `qlib`, `qlib_test`, `qlib_harness_selftest` and `fuzz_vector` all get the flags without naming them individually.
- It is **not** an `INTERFACE` target. The flags must reach the tests, which an interface target linked only to `qlib` would not do, and they must not become part of the installed target's interface, which a `PUBLIC` link would cause. `add_link_options` also covers the link step, which sanitizers require.

## Optional modules

`QLIB_BUILD_NET` is the first module that can be switched off, and switching it
off has to remove more than its source. Four things are tied together:

| Where | When the module is built | When it is not |
|---|---|---|
| `QLIB_SRC` | `src/ql_net.c` appended | not appended |
| Target definition | `QLIB_HAS_NET` added as a `PUBLIC` compile definition | not added |
| Installed headers | `ql_net.h` installed | excluded, via `PATTERN "ql_net.h" EXCLUDE` in the directory install |
| `qlib.pc` | `Cflags` gains `-DQLIB_HAS_NET` | `Cflags` unchanged |

The `PUBLIC` definition is what makes it work for CMake consumers, and the
pkg-config entry is what makes it work for everyone else. Both exist because
`include/qlib/qlib.h` includes the module's header only under
`#ifdef QLIB_HAS_NET`:

```c
#ifdef QLIB_HAS_NET
#  include "qlib/ql_net.h"
#endif
```

Without that guard the umbrella header would break in a build without the
module. Without the conditional install, a disabled build would hand out a
header whose symbols do not exist, and the failure would surface later as an
undefined reference rather than immediately. Test, fuzz and benchmark source
lists are conditional for the same reason, and their registration points
(`tests/test_main.c`, `bench/bench_main.c`) use the same macro.


## Adding a new module

The sequence below is what `ql_vector` did, and it is the checklist for the next one. Replace `foo` with the module name.

1. **Write the public header** `include/qlib/ql_foo.h`. Start with a fresh include guard, include `<stddef.h>` and `"qlib/qlib_export.h"` (plus `"qlib/ql_common.h"` and `"qlib/ql_mem.h"` if needed), and wrap everything in `extern "C"` guards. Mark every exported function `QLIB_EXPORT`, and add `QLIB_NODISCARD` where ignoring the result would be a bug ([CONTRIBUTING.md](../../CONTRIBUTING.md) rules 9 and 10).
2. **Write the implementation** `src/ql_foo.c`. Define any opaque `struct ql_foo` here. Include `"ql_internal.h"` if the module needs to report errors, and call `ql_err_raise()` rather than touching the handler directly (rule 11). Route every allocation through the caller's `ql_allocator_t` (rule 7).
3. **Register the source** in the `QLIB_SRC` list in [CMakeLists.txt](../../CMakeLists.txt).
4. **Add the header to the umbrella**: one `#include "qlib/ql_foo.h"` line in [`qlib.h`](../../include/qlib/qlib.h). No install rule change is needed — `install(DIRECTORY include/qlib ... FILES_MATCHING PATTERN "*.h")` picks it up automatically.
5. **Write the test suite** `tests/test_foo.c`. Include `"test_suites.h"` first, then the module header under test. End the file with `MU_TEST_SUITE(test_suite_foo) { ... }`.
6. **Declare the suite** in [`tests/test_suites.h`](../../tests/test_suites.h): add `MU_DECLARE_SUITE(test_suite_foo);`. This is what keeps `-Wmissing-prototypes` quiet and guarantees the declaration matches the definition.
7. **Run the suite** from [`tests/test_main.c`](../../tests/test_main.c): add `MU_RUN_SUITE(test_suite_foo);` before `MU_REPORT()`.
8. **Add the test source** to the `add_executable(qlib_test ...)` list in [`tests/CMakeLists.txt`](../../tests/CMakeLists.txt).
9. **Add a fuzz target** if the module parses or transforms input: `fuzz/fuzz_foo.c` with `int LLVMFuzzerTestOneInput(const uint8_t*, size_t)`, forward-declared so `-Wmissing-prototypes` stays quiet, plus an `add_executable` block and an optional `add_test` smoke run in [`fuzz/CMakeLists.txt`](../../fuzz/CMakeLists.txt).
10. **Add benchmark cases** for anything whose cost a user might ask about: a `ql_bench_case` entry in [`bench/`](../../bench/CMakeLists.txt), with the correctness of the measured result asserted through `QL_BENCH_CHECK`. Put untimed work in the `setup` hook so it is not charged to the operation being measured.
11. **Verify before committing**:

```bash
cmake -S . -B build -DQLIB_WERROR=ON -DQLIB_BUILD_BENCH=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/bin/qlib_bench

clang-format -i src/*.c include/qlib/*.h tests/*.c tests/*.h bench/*.c bench/*.h
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy src/*.c tests/*.c bench/*.c -p build
```

The same steps are enforced by the `format`, `tidy`, `linux`, `macos`, `windows`, `sanitizers`, `fuzz`, `docs` and `package` jobs in [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml); see [testing](testing.md) for what each job guards.
