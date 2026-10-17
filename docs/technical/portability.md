# Portability

## Support matrix

Platform/compiler combinations that CI builds and tests, versus those that are merely expected to work.

| Platform | Compiler | Status | How it is covered |
|---|---|---|---|
| Linux | GCC | Supported | `linux / gcc` job, `ubuntu-24.04`, `-DQLIB_WERROR=ON` |
| Linux | Clang | Supported | `linux / clang` job |
| Linux | Clang + ASAN/UBSAN | Supported | `sanitizers` job, `-DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` |
| Linux | Clang + libFuzzer | Supported | `fuzz` job, `-DQLIB_BUILD_FUZZ=ON` |
| macOS | AppleClang | Supported | `macos` job, `macos-latest` |
| Windows | MSVC | Supported | `windows` job, static and shared matrices |
| Windows | MinGW / clang-cl | Not tested | — |
| Any other Unix | GCC / Clang | Untested | Expected to work: no platform code beyond `errno` constants exists yet |

macOS is covered by the `macos` job even though the project's headline
description only names Linux and Windows; the CI job is the authority for what
is actually exercised.

## C11 baseline

The baseline is C11 with compiler extensions disabled. [CONTRIBUTING.md](../../CONTRIBUTING.md)
rule 1 states it, and [CMakeLists.txt](../../CMakeLists.txt) enforces it:

```cmake
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Consumers additionally inherit a C11 floor.
target_compile_features(qlib PUBLIC c_std_11)
```

The first three are the directory defaults for the `C_STANDARD`,
`C_STANDARD_REQUIRED` and `C_EXTENSIONS` target properties, so they cover the
library, the tests and the fuzz targets alike. `C_STANDARD_REQUIRED` together
with `C_EXTENSIONS OFF` is what asks for `-std=c11` rather than `-std=gnu11`.

Verified from `compile_commands.json`: every first-party target is compiled with
`-std=c11` and nothing else, and the tree builds clean under `-Wpedantic -Werror`
in that mode.

Both mechanisms are present because they do different jobs:

| Mechanism | Effect |
|---|---|
| `CMAKE_C_STANDARD` + `C_STANDARD_REQUIRED` + `C_EXTENSIONS OFF` | Pins qlib's own targets to exactly `-std=c11` |
| `target_compile_features(qlib PUBLIC c_std_11)` | Requires a C11 *minimum* of anything linking `qlib::qlib`, without forcing a dialect onto the consumer |

`target_compile_features()` alone is not sufficient: it expresses a floor, so a
compiler whose default dialect already satisfies `c_std_11` receives no `-std=`
flag at all and builds as whatever it defaults to — C23 on GCC 16, for instance.
That was the state of this repository before the dialect was pinned explicitly.

Code must not depend on anything newer than C11 or on compiler extensions. The
single deliberate relaxation is [`ql_common.h`](../../include/qlib/ql_common.h),
where `QLIB_NODISCARD` may use newer spellings when the compiler offers them —
the fallback chain below guarantees an empty expansion on a strict C11 compiler.
GCC and Clang accept `__attribute__` in `-std=c11` mode, so the attribute path
compiles under the pinned dialect.

## The `QLIB_NODISCARD` fallback chain

Declared in [`ql_common.h`](../../include/qlib/ql_common.h) and attached to every function whose result must not be ignored — the allocation helpers and the mutating vector operations.

```c
#if defined(__cplusplus) && __cplusplus >= 201703L
#  define QLIB_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#  define QLIB_NODISCARD __attribute__((warn_unused_result))
#elif defined(_Check_return_)
#  define QLIB_NODISCARD _Check_return_
#else
#  define QLIB_NODISCARD
#endif
```

| Order | Guard | Expansion | Notes |
|---|---|---|---|
| 1 | `__cplusplus >= 201703L` | `[[nodiscard]]` | C++17 and later. |
| 2 | `__GNUC__` or `__clang__` | `__attribute__((warn_unused_result))` | The path taken for all C compilation, including clang-cl, which defines `__clang__`. |
| 3 | `_Check_return_` | `_Check_return_` | MSVC, and only if SAL has already been included by something else — the macro is tested with `#ifdef` rather than assumed. |
| 4 | — | empty | Any other compiler. The code still compiles; the diagnostic is simply lost. |

Two limitations follow from this ordering. C23's `[[nodiscard]]` is *not* detected for C compilation — there is no `__STDC_VERSION__` branch — so a C23 compiler falls through to the GCC/Clang attribute, which is equivalent in effect. And the chain is compile-time only; `QLIB_NODISCARD` produces a warning, so it only becomes a build failure where `-Werror` or `/WX` is active, which in this project means `-DQLIB_WERROR=ON` or CI.

Verified behaviour on GCC: ignoring `ql_alloc(NULL, 4);` produces `warning: ignoring return value of 'ql_alloc' declared with attribute 'warn_unused_result' [-Wunused-result]`.

## `ql_ret_from_errno()` and `#ifdef`-guarded cases

`ql_ret_from_errno()` maps a platform error number onto `ql_ret_t`. It lives in `src/ql_common.c` and is the only place in the library that includes `<errno.h>`.

The POSIX `errno` set is much wider than the MSVC one. Six cases are therefore guarded, because the constant may not be defined at all:

| Unconditional | Guarded with `#ifdef` |
|---|---|
| `0`, `ENOMEM`, `EINVAL`, `ERANGE`, `ENOENT`, `EIO` | `EEXIST`, `EAGAIN`, `ETIMEDOUT`, `EBUSY`, `EOVERFLOW`, `ENOTSUP` |

Any number that matches no case falls through to `QL_ERR_SYS`, so an unmapped error is still reported rather than dropped.

A second constraint shapes the list: **aliases are deliberately not used.** On glibc `EWOULDBLOCK == EAGAIN` and `EOPNOTSUPP == ENOTSUP`. Writing both spellings of an alias into one `switch` produces two `case` labels with the same value, which C rejects as a compile error. The comment in `src/ql_common.c` records this so the omission is not mistaken for an oversight. Since an alias is the same integer, the primary name already matches every value the alias could have.

## Annex K (`*_s`) functions are not used

The C11 standard defines an optional bounds-checked library, Annex K, providing `memcpy_s`, `memset_s`, `memmove_s` and friends. This project does not use it, and the corresponding clang-tidy check is disabled in [`.clang-tidy`](../../.clang-tidy) with that justification.

Annex K is optional, and glibc and macOS libc do not provide it. Adopting `memcpy_s` in `src/ql_vector.c` would break two of the three platforms CI supports, and there is no portable shim that would not just reimplement the check. The plain `memcpy`, `memmove` and `memset` calls in every module and in the minunit harness are therefore the intended tools.

## Aligned allocation without a platform branch

Over-aligned allocation is the one place where the obvious implementation is
platform-specific: C11 `aligned_alloc`, POSIX `posix_memalign` and MSVC
`_aligned_malloc`/`_aligned_free` each exist on a subset of the supported
platforms, and the MSVC pair even takes its arguments in the opposite order.

None of them is used. `ql_aligned_alloc()` takes `alignment + sizeof(void*)`
bytes of slack from the allocator's own `malloc`, rounds the address up, and
stores the original pointer immediately before the aligned block; `ql_aligned_free()`
reads that header back. The result is a single code path that compiles
identically everywhere, needs no `#if defined(_WIN32)`, and — because the
allocation still comes from the caller's allocator — keeps the rule that all
memory goes through `ql_allocator_t`.

The trade-offs are recorded in ADR-13 of [design decisions](design-decisions.md).
The portability-relevant part is that this file contains no aligned-allocation
platform guard to test, which is why the support matrix above has no per-platform
caveat for it.

## Why `ql_lock_t` was removed

An earlier revision declared a lock adapter (`ql_lock_t`, `ql_lock_fn`, `ql_unlock_fn`) in `ql_common.h`. It was deleted rather than implemented, for a portability reason among others:

- A working implementation needs two code paths — POSIX threads and Win32 `CRITICAL_SECTION`.
- The test for such a thing has to start real threads. A single portable test would require C11 `<threads.h>`, which **macOS does not provide**; the test would therefore need its own two platform paths on top of the implementation's.
- No concurrent container exists yet to consume it.

Shipping the declarations without the implementation was rejected because untested cross-platform threading code is more expensive than an absent feature. The full reasoning is in ADR-12 of [design decisions](design-decisions.md); `ql_allocator_t.reserved[4]` is the reserved room for its return.

## Warning flags

Applied through the `qlib_warnings` interface target, which every first-party target links (see [build system](build-system.md)).

| Compiler | Flags |
|---|---|
| GCC, Clang, AppleClang | `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wwrite-strings` |
| MSVC | `/W4` |
| Any, when `QLIB_WERROR=ON` | GCC/Clang: `-Werror`; MSVC: `/WX` |

`-Wmissing-prototypes` and `-Wstrict-prototypes` are the reason test and fuzz entry points declare their prototypes explicitly: `LLVMFuzzerTestOneInput` is forward-declared in `fuzz/fuzz_vector.c`, and suite functions are declared through `MU_DECLARE_SUITE` in `tests/test_suites.h`.

`QLIB_WERROR` defaults to `OFF`. The header of [CMakeLists.txt](../../CMakeLists.txt) does not hard-code it, so a consumer building with a newer compiler is not blocked by a warning they did not cause. CI sets `-DQLIB_WERROR=ON` in every build job.

## `QLIB_STATIC_DEFINE` for non-CMake users on Windows

The generated `qlib_export.h` selects `__declspec(dllexport)` / `__declspec(dllimport)` / nothing based on `QLIB_STATIC_DEFINE`. CMake consumers get the right answer automatically: the static target adds `QLIB_STATIC_DEFINE` as a `PUBLIC` compile definition, so it reaches anything linking `qlib::qlib`.

There is no such help outside CMake. A user compiling against the **static** library on Windows with MSVC, by hand or with another build system, must define it themselves:

```bat
cl /DQLIB_STATIC_DEFINE /I<prefix>\include app.c <prefix>\lib\qlib.lib
```

Without it the header expands `QLIB_EXPORT` to `__declspec(dllimport)` and the link fails. This is also noted in the README under "Using qlib". Consumers of the **DLL** must not define it.

## Sanitizers

`QLIB_SANITIZE=ON` applies flags with directory scope so that they reach every target in the project, including tests and fuzz targets:

| Compiler | Compile and link flags |
|---|---|
| GCC / Clang | `-fsanitize=address,undefined -fno-omit-frame-pointer -g` |
| MSVC | `/fsanitize=address` only |

MSVC ships AddressSanitizer but has no UndefinedBehaviorSanitizer, so `QLIB_SANITIZE` there is weaker than on the other two platforms. On Linux, ASAN also brings LeakSanitizer, which is why the CI job is described as `asan+ubsan` and catches leaks without extra configuration.

## Threads

`ql_thread` is the only part of the library that is not plain C11, and it is
optional (`QLIB_BUILD_THREADS`). The split it hides:

| Concept | POSIX | Windows |
|---|---|---|
| Thread | `pthread_t`, `pthread_create` | `_beginthreadex` |
| Mutex | `pthread_mutex_t` | `CRITICAL_SECTION` |
| Condition variable | `pthread_cond_t` | `CONDITION_VARIABLE` |
| Timed wait | `pthread_cond_timedwait` | `SleepConditionVariableCS` |
| CPU count | `sysconf(_SC_NPROCESSORS_ONLN)` | `GetSystemInfo` |

Two details are worth knowing:

**`_beginthreadex`, not `CreateThread`.** A thread that runs C runtime code has
to let the CRT set up and tear down its per-thread state; `CreateThread` skips
that and leaks it.

**`timespec_get`, not `clock_gettime`.** The latter is POSIX.1b and is hidden
under `-std=c11`, which is exactly the dialect this library builds in. C11's
`timespec_get(TIME_UTC)` gives the same wall clock that a default
`pthread_cond_timedwait` measures against.

The Windows implementation is verified by CI only; it cannot be compiled on the
development machine. See [ADR-17](design-decisions.md).
