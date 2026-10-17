# ABI and Versioning

## What ABI stability means here

The headers and the compiled library are versioned independently: a program is compiled against one set of headers and links against one build of `libqlib`. ABI stability means a binary built against an older library keeps working with a newer one, without recompiling, as long as the major version is unchanged.

Three properties make that possible in this codebase:

| Property | Where it is enforced |
|---|---|
| Callers never compute the size or layout of a library-owned object | `ql_vector_t` is opaque; `struct ql_vector` lives only in `src/ql_vector.c` (see [architecture](architecture.md)) |
| Exposed value structs can grow without moving existing members | `ql_allocator_t.reserved[4]` in [`ql_mem.h`](../../include/qlib/ql_mem.h) (see [design decisions](design-decisions.md)) |
| Existing enum values never change their numeric value | `ql_ret_t` is append-only |

## The append-only rule for `ql_ret_t`

`ql_ret_t` is a plain enum with implicit numbering from `QL_OK = 0`:

```c
typedef enum
{
    QL_OK = 0,
    QL_ERR_NOMEM,         /* 1 */
    ...
    QL_ERR_OVERFLOW,      /* 13 */
} ql_ret_t;
```

The header states the rule directly: *"New codes are only ever appended. The numeric values are part of the ABI and must not be reordered or renumbered."*

The rule matters because the value crosses the boundary as an `int`. Renumbering `QL_ERR_INVALID_ARG`, or inserting a new code in the middle, silently changes the meaning of every `ql_ret_t` a caller has already stored, compared or logged. Appending keeps existing binaries correct and only makes them unaware of codes they were never compiled to expect — which is why `ql_strerror()` has to be total (see ADR-7 in [design decisions](design-decisions.md)).

## Symbol export

### `QLIB_EXPORT`

Every public function carries `QLIB_EXPORT`, applied at the declaration in the header. [CONTRIBUTING.md](../../CONTRIBUTING.md) rule 9 makes this mandatory for new public functions.

The macro comes from CMake's `GenerateExportHeader` module, configured in [CMakeLists.txt](../../CMakeLists.txt):

```cmake
generate_export_header(qlib
    BASE_NAME QLIB
    EXPORT_MACRO_NAME QLIB_EXPORT
    EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/qlib/qlib_export.h"
)
```

The generated file is `include/qlib/qlib_export.h` **in the build tree** — it does not exist in the source tree, and each public header includes it. A consequence worth knowing: pointing a compiler at the source tree with only `-Iinclude` fails, because the generated header is not there. Both `include/` and `<build>/include/` are needed, which is exactly what the build interface and the installed interface provide:

| Consumer | Include directories |
|---|---|
| Build tree | `$<BUILD_INTERFACE:.../include>` and `$<BUILD_INTERFACE:.../<build>/include>` |
| Installed | `$<INSTALL_INTERFACE:include>`, and `qlib_export.h` is installed next to the other headers |

### `QLIB_STATIC_DEFINE`

A static library and a DLL need opposite declarations on Windows: consuming a DLL requires `__declspec(dllimport)`, consuming a static library requires nothing at all. The generated header resolves this with `QLIB_STATIC_DEFINE`:

```cmake
# A static build must not expand QLIB_EXPORT to dllimport on Windows.
if(NOT QLIB_SHARED)
    target_compile_definitions(qlib PUBLIC QLIB_STATIC_DEFINE)
endif()
```

`PUBLIC` means the definition propagates: anything linking `qlib::qlib` in the build tree gets it automatically. Verified on this checkout — the test targets compile with `-DQLIB_STATIC_DEFINE` in a default (static) build.

### ELF versus PE

The same `QLIB_EXPORT` token expands differently per object format.

| | ELF (Linux, macOS) | PE (Windows/MSVC) |
|---|---|---|
| Building the library | `__attribute__((visibility("default")))` | `__declspec(dllexport)` |
| Consuming the library | `__attribute__((visibility("default")))` | `__declspec(dllimport)` |
| Consuming statically | expands to nothing (`QLIB_STATIC_DEFINE`) | expands to nothing |
| Selection macro | none needed | `qlib_EXPORTS`, defined by CMake when compiling the `SHARED` target |

On ELF the two branches are textually identical apart from a comment, because the default is already public visibility — `C_VISIBILITY_PRESET` is not set anywhere in [CMakeLists.txt](../../CMakeLists.txt). The practical effect is that `QLIB_EXPORT` is not what makes symbols visible on Linux; it is what makes them visible on Windows. Internal helpers such as `ql_err_raise()` are therefore *not* hidden on ELF even though they are absent from the Windows import library. Verified: `nm -D` on a shared build lists `ql_err_raise` alongside the public API.

An invariant that follows: only declarations marked `QLIB_EXPORT` are covered by the compatibility promises in this document.

## Version numbers

Two independent version mechanisms exist, and they answer different questions.

| Mechanism | Source | Question it answers |
|---|---|---|
| `VERSION 0.3.0` / `SOVERSION 0` | `project(qlib VERSION 0.3.0 ...)` in [CMakeLists.txt](../../CMakeLists.txt) | Which binary does the loader pick? |
| `QLIB_VERSION_MAJOR/MINOR/PATCH/STRING` | [`ql_common.h`](../../include/qlib/ql_common.h) | Which headers was this translation unit compiled against? |
| `ql_version()` | `src/ql_common.c`, returns `QLIB_VERSION_STRING` | Which library is actually loaded at runtime? |

`VERSION` and `SOVERSION` become the ELF versioning of the shared object. On this checkout a shared build produces:

```text
libqlib.so -> libqlib.so.0 -> libqlib.so.0.3.0
SONAME: libqlib.so.0
```

`SOVERSION` is `${PROJECT_VERSION_MAJOR}`, so the loader demands major version 0 and accepts any 0.x library that is present.

### Macros versus `ql_version()`

The `QLIB_VERSION_*` macros are baked into whichever headers a translation unit was compiled with. `ql_version()` reports the library that is loaded at run time. When program, headers and library all come from one build these agree. They disagree in the situation that matters: an application compiled against new headers but loading an older `libqlib.so` from a stale install path, or the reverse.

Because the library can only promise that a minor upgrade is compatible in one direction, the check is worth doing explicitly during start up:

```c
if (strcmp(ql_version(), QLIB_VERSION_STRING) != 0) {
    /* headers and library are not from the same build */
}
```

The header documents this. Note that `ql_version()` returns a `const char*`, so the comparison is a string comparison, not a numeric one — `QLIB_VERSION_MAJOR` and friends are the numeric form when a program needs to branch on a version feature.

## CMake package version

The CMake package records a compatibility policy, not just a number:

```cmake
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/qlibConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
```

`SameMajorVersion` means a consumer's `find_package(qlib 0.1 REQUIRED)` accepts any installed `0.x.y`, and rejects `1.x`. Verified behaviour: with `0.3.0` installed, `find_package(qlib 0.1 REQUIRED)` succeeds; requesting `9.9` fails with *"Could not find a configuration file for package "qlib" that is compatible with requested version "9.9""*. The resolver reports the installed version as `0.3.0`.

`SameMajorVersion` is the right policy here only while the major version is bumped for every incompatible change. Given the version is still `0.x`, the stricter reading is that **any** release before `1.0.0` may break compatibility, and the package version check will not catch it, because all `0.x` versions are mutually acceptable.

## What may change in a minor release

Allowed without changing the major version:

- Adding new functions, new headers, new opaque types.
- Adding new enumerators to the end of `ql_ret_t`.
- Adding new fields to a private (opaque) struct's definition.
- Taking a slot in `ql_allocator_t.reserved[]`, provided existing members keep their positions and `ql_default_allocator()` populates the new slot.
- Changing any implementation detail, growth policy, or internal file layout.

Not allowed:

- Removing or renaming a public function, or changing its parameter list or return type.
- Changing the numeric value of any `ql_ret_t` enumerator, or inserting a new one anywhere but the end.
- Reordering or resizing `ql_allocator_t`, or removing members — including `reserved[]`.
- Making a previously complete type opaque, or exposing the layout of a currently opaque type.
- Changing documented ownership or lifetime rules, such as "on failure the original block is still owned by the caller" in `ql_realloc()`.

A change to the size or layout of `ql_allocator_t`, or the removal of any exported function, is a major-version change. The same applies to any behaviour callers can observe through a documented guarantee, even where the signature is untouched.
