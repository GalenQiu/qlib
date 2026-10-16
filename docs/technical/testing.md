# Testing

## Strategy

The test suite has four layers, each answering a different question:

| Layer | Question it answers | Where |
|---|---|---|
| Unit tests | Does the documented behaviour hold, including every error path? | `tests/test_*.c` |
| Harness self check | Does a failing assertion actually fail the run? | `tests/selftest/` |
| Fuzzing | Does arbitrary operation sequences break memory safety or an invariant? | `fuzz/fuzz_vector.c` |
| Mutation testing | Would the tests notice if the implementation were wrong? | Manual, see below |

The first three run in CI on every push. The fourth is a review technique, not an automated gate — its purpose is to find tests that pass for the wrong reason.

Coverage is currently 39 test cases: 5 in `test_common.c`, 12 in `test_mem.c`, 22 in `test_vector.c`. They all run in a single binary, `qlib_test`, registered with ctest as `qlib_common_test`.

## The minunit harness

[`tests/minunit.h`](../../tests/minunit.h) provides the whole harness in five macros:

| Macro | Purpose |
|---|---|
| `MU_TEST(name)` | Declares a test case as `static void name(void)` |
| `MU_TEST_SUITE(name)` | Declares a suite function that calls `MU_RUN_TEST` |
| `MU_DECLARE_SUITE(name)` | Emits `void name(void);` so the runner and the definer agree on linkage |
| `MU_RUN_TEST(test)` | Increments `mu_tests_run`, then calls the test |
| `MU_CHECK(cond)` | Records a failure and continues |
| `MU_ASSERT(cond)` | Records a failure and returns from the test |
| `MU_REPORT()` / `MU_EXIT_CODE` | Print the tally; exit non-zero if anything failed |

`MU_RUN_TEST` increments the counter *before* calling the test, so a test that crashes still counts as having run.

### The bug this harness originally had

The counters were defined in the header:

```c
/* wrong — one private copy per translation unit */
static int mu_tests_run = 0;
static int mu_failures = 0;
```

A `static` definition in a header gives **every** `.c` file its own copy. `MU_CHECK` is expanded inside `test_common.c`, so a failing assertion incremented *that* file's `mu_failures`. `MU_REPORT` and `MU_EXIT_CODE` live in `test_main.c` and read *its* copy, which stayed zero. The observable result was:

```text
FAIL: tests/test_common.c:33: 1 == 2      <- the assertion did fail
0 tests run, 0 failures                   <- the report said otherwise
exit status 0                             <- and the process exited successfully
```

A red test looked green, so CI could never have caught a regression.

The fix is a single definition plus external declarations:

```c
/* minunit.h */
extern int mu_tests_run;
extern int mu_failures;

/* minunit.c */
int mu_tests_run = 0;
int mu_failures = 0;
```

[`tests/minunit.c`](../../tests/minunit.c) exists for no other reason. Both `.c` files are listed in `tests/CMakeLists.txt` so the counters are genuinely shared across the translation-unit boundary.

### `MU_ASSERT` alongside `MU_CHECK`

`MU_CHECK` continues executing after a failure. That is what you want for a sequence of independent comparisons, and it is wrong whenever the next statement would dereference the thing just checked:

```c
MU_CHECK(state.message != NULL);
MU_CHECK(strlen(state.message) > 0);   /* crashes if the check above failed */
```

`MU_ASSERT` records the failure and returns, so the test aborts cleanly and the remaining cases still run. The rule of thumb used throughout this suite:

- `MU_CHECK` for values that are already safe to compare.
- `MU_ASSERT` for a pointer whose target is dereferenced afterwards, or a value used as a divisor or index.

This is not hypothetical: mutation testing (below) exposed exactly this pattern in `tests/test_mem.c`, where a mutated implementation made the test segfault instead of reporting a clean failure. It now reads `MU_ASSERT(state.message != NULL);`.

### Adding a suite

1. Create `tests/test_foo.c`, include `"test_suites.h"` first, and end the file with a `MU_TEST_SUITE(test_suite_foo) { ... }` block.
2. Add `MU_DECLARE_SUITE(test_suite_foo);` to [`tests/test_suites.h`](../../tests/test_suites.h).
3. Add `MU_RUN_SUITE(test_suite_foo);` to [`tests/test_main.c`](../../tests/test_main.c) before `MU_REPORT()`.
4. Add `test_foo.c` to the `add_executable(qlib_test ...)` list in [`tests/CMakeLists.txt`](../../tests/CMakeLists.txt).

Step 2 is not optional bookkeeping. `MU_TEST_SUITE` defines a function with external linkage, and `-Wmissing-prototypes` (part of the project's warning set) rejects an external definition with no visible prototype. `MU_DECLARE_SUITE` is included by both the defining file and the runner, so the prototype and the definition cannot disagree — which is what went wrong when `test_main.c` hand-wrote `static void test_suite_common(void);` against a non-static definition.

## The harness self check

`qlib_harness_selftest` is an executable containing a test that is **supposed to fail** ([`tests/selftest/harness_suite.c`](../../tests/selftest/harness_suite.c)):

```c
MU_TEST(test_intentional_failure)
{
    int actual = 1;
    int expected = 2;

    MU_CHECK(actual == expected);
}
```

It lives in its own translation unit from the runner ([`harness_main.c`](../../tests/selftest/harness_main.c)) precisely so the counters must cross a TU boundary — the property whose absence caused the original bug. The comparison goes through variables so no compiler's `-Wtautological-compare` fires under `-Werror`.

The suite is registered with ctest through a driver script rather than a direct command:

```cmake
add_test(NAME qlib_harness_selftest
    COMMAND ${CMAKE_COMMAND}
        "-DPROBE=$<TARGET_FILE:qlib_harness_selftest>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/selftest/expect_failure.cmake"
)
```

[`expect_failure.cmake`](../../tests/selftest/expect_failure.cmake) runs the probe and asserts two things:

1. the exit status is non-zero, and
2. the combined stdout/stderr contains exactly `1 tests run, 1 failures`.

### Why `WILL_FAIL` was rejected

ctest has a built-in `WILL_FAIL` property that inverts a test's result, and it was the obvious first choice. It inverts on **any** non-zero exit, which includes a segfault, an abort, or a sanitizer failure. A probe that crashed before reaching its assertion would therefore be recorded as a pass — the guard would silently stop guarding. Checking the reported counts as well as the status means the probe must genuinely reach `MU_REPORT` with the failure recorded.

Verified behaviour of the driver, all three paths:

| Probe | Result |
|---|---|
| a binary that exits 0 | `FATAL_ERROR: ... a failing assertion exited with success.` |
| `/bin/false` (exits 1, no report) | `FATAL_ERROR: ... expected '1 tests run, 1 failures' in the report.` |
| the real `qlib_harness_selftest` | `test harness self check passed (exit=1, report matched)` |

`CONTRIBUTING.md` notes that this suite must not be "fixed".

## The `tests/.clang-tidy` override

[`.clang-tidy`](../../.clang-tidy) applies to the whole repository. [`tests/.clang-tidy`](../../tests/.clang-tidy) uses `InheritParentConfig: true` and disables two checks:

| Check | Why |
|---|---|
| `cert-err33-c` | `MU_CHECK`/`MU_ASSERT` expand to a `fprintf()` whose result is deliberately ignored, so every single assertion trips it. It stays enabled for `src/`, where an unchecked return value is a real defect. |
| `readability-function-cognitive-complexity` | A metric aimed at production logic. A test function is legitimately a long linear sequence of assertions. |

Inheriting rather than duplicating keeps the enabled-check list in one place. Note that the root config also disables `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` and `readability-implicit-bool-conversion`; those exclusions apply to tests too and are explained in the root file.

## Fuzzing

[`fuzz/fuzz_vector.c`](../../fuzz/fuzz_vector.c) is a libFuzzer target, built only when `QLIB_BUILD_FUZZ=ON` and the compiler is Clang (`-fsanitize=fuzzer`). With any other compiler, [`fuzz/CMakeLists.txt`](../../fuzz/CMakeLists.txt) emits a `WARNING` instead of silently producing nothing.

### What the input bytes mean

The input is a script of vector operations, one byte per operation:

| Byte | Meaning |
|---|---|
| `data[0]` | Element size: `(data[0] % 32) + 1`, so sizes 1..32 |
| `data[i]` for `i >= 1` | `op = data[i] % 8`, `index = data[i] % 64` |

| `op` | Operation |
|---|---|
| 0 | `ql_vector_push_back(vec, elem)` |
| 1 | `ql_vector_pop_back(vec, NULL)` |
| 2 | `ql_vector_insert(vec, index, elem)` |
| 3 | `ql_vector_remove(vec, index)` |
| 4 | `ql_vector_reserve(vec, index)` |
| 5 | `ql_vector_resize(vec, index)` |
| 6 | `ql_vector_push_back(vec, ql_vector_at(vec, ...))` — deliberate self-aliasing |
| 7 | `ql_vector_assign(vec, at, ql_vector_at(vec, at))` — overlapping self-assignment |

Operations 6 and 7 exist specifically to drive the aliasing paths from ADR-9 in [design decisions](design-decisions.md). Indices are capped at 64 by `FUZZ_MAX_INDEX` so the fuzzer spends its time on state transitions rather than on multi-gigabyte allocations.

The element payload is a `uint8_t elem[32]` buffer filled from the input. Its size is the reason it is a 32-byte array and not a scalar: an earlier revision used a `uint32_t` while `elem_size` could be 32, so `ql_vector_insert` read past the end of a 4-byte stack object. ASAN flagged it as a `stack-buffer-overflow` within 5000 runs. That was a bug in the harness, not the library, and the corrective comment is in the source.

### Invariants checked after every operation

`check_invariants()` runs after each byte and `abort()`s on violation, so libFuzzer records the input as a crash:

- `ql_vector_size(vec) <= ql_vector_capacity(vec)`.
- Every live slot is addressable: `ql_vector_at(vec, i) != NULL` for all `i < size`.
- One past the end is not addressable: `ql_vector_at(vec, size) == NULL`.

### Results

A fresh run on this checkout:

```text
Done 6044768 runs in 46 second(s)
stat::average_exec_per_sec:     131408
```

No findings. Note that the target is compiled with `-fsanitize=fuzzer,address,undefined`, so these runs also cover ASAN and UBSAN.

When `QLIB_BUILD_FUZZ=ON`, ctest additionally registers `fuzz_vector_smoke` (`-runs=2000 -max_len=128`) so a normal `ctest` run exercises the target too.

## Mutation testing

The technique: break one guarantee in the implementation, rebuild, and confirm the suite goes red. A mutation that survives means the corresponding behaviour is untested.

Run against the current code with an ASAN+UBSAN Debug build (`-DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`):

| # | Mutation | Result |
|---|---|---|
| A | `ql_size_mul_overflow()`: replace the guard with `if (0)` | exit 1; ASAN reports `requested allocation size 0xfffffffffffffffe ... exceeds maximum supported size` in `ql_def_malloc` |
| B | `ql_calloc()`: remove `memset(ptr, 0, total)` | exit 1; `39 tests run, 64 failures` |
| C | `ql_alloc()`: remove the `QL_ERR_NOMEM` raise | exit 1; `39 tests run, 3 failures` |
| D | `ql_vector_push_back`/`insert`: bypass `ql_vector_pin_elem()` | exit 1; ASAN reports `heap-use-after-free` |

Two observations worth recording:

- Mutation A is caught as a *memory-safety* failure, not merely as failed assertions. Without the overflow check, a wrapped `SIZE_MAX * 2` reaches `malloc` and ASAN refuses it. The guard's value is therefore visible even to a build that has no test for that specific case.
- Mutation C initially made the suite **segfault** rather than report a failure, because `test_failure_raises_nomem` followed a failing `MU_CHECK(state.message != NULL)` with `strlen(state.message)`. That is the concrete reason `MU_ASSERT` exists, and the reason mutation testing is worth doing: the test would have been reported as a crash, not as a clean failure.

The mutation edits are applied and reverted mechanically, and the working tree was verified to be byte-identical afterwards.

A fifth "mutation" is permanent rather than manual: the harness self check is a test that is supposed to fail, so reintroducing the static-counter bug makes the probe exit 0 and the ctest driver fail loudly.

## Sanitizers

`QLIB_SANITIZE=ON` adds `-fsanitize=address,undefined -fno-omit-frame-pointer -g` (GCC/Clang) or `/fsanitize=address` (MSVC, which has no UBSan) with directory scope, so the library, tests, harness probe and fuzz target are all instrumented. On Linux, ASAN includes LeakSanitizer, which catches leaked blocks without additional configuration.

```bash
cmake -S . -B build -DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## CI jobs

Defined in [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml). Every build job passes `-DQLIB_WERROR=ON`, and `CTEST_OUTPUT_ON_FAILURE=1` is set globally so a failing test prints its output.

| Job | Runner | Configuration | What it guards |
|---|---|---|---|
| `linux` | `ubuntu-24.04`, matrix `gcc` / `clang` | `-DQLIB_WERROR=ON` | Both Linux compilers build warning-free and pass the suite |
| `macos` | `macos-latest` | `-DQLIB_WERROR=ON` | AppleClang builds and passes — the third claimed platform |
| `windows` | `windows-latest`, matrix `shared` `OFF`/`ON` | `-DQLIB_WERROR=ON -DQLIB_SHARED=...` | MSVC builds; the `ON` variant is the only place `QLIB_EXPORT`, `__declspec(dllexport)` and `QLIB_STATIC_DEFINE` are exercised for real |
| `sanitizers` | `ubuntu-24.04`, clang | `-DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` | No memory errors or undefined behaviour in the unit suite |
| `fuzz` | `ubuntu-24.04`, clang | `-DQLIB_BUILD_FUZZ=ON`, then `-max_total_time=120 -max_len=256` | Memory safety and invariants under arbitrary operation sequences; uploads `crash-*` as the `fuzz-crash` artifact on failure |
| `package` | `ubuntu-24.04` | install to a prefix, then build two consumers | `find_package(qlib 0.1 REQUIRED)` and `pkg-config` both resolve and link |
| `docs` | `ubuntu-24.04` | `-DQLIB_BUILD_DOCS=ON`, then `--target docs` | `Doxyfile.in` still parses and still produces `docs/html/index.html`; uploads the generated reference as the `api-reference` artifact |
| `format` | `ubuntu-24.04` | `pip install clang-format==23.1.1` | Formatting of every file from `git ls-files '*.c' '*.h'`, with the version pinned because output differs between releases |
| `tidy` | `ubuntu-24.04` | clang-tidy 23 from LLVM's apt repository | Static analysis of `src/*.c` and `tests/*.c` via `-p build`; `WarningsAsErrors: '*'` makes any finding fatal |

Two gaps are worth stating explicitly. The `tidy` job analyses `src/` and `tests/` only, so `fuzz/fuzz_vector.c` is not covered by clang-tidy — it is not in the compilation database unless `QLIB_BUILD_FUZZ=ON`. And the `format` job's `git ls-files` covers tracked files only, so a new source file must be added to the index before CI will check it.
