# Development workflow

This document is for **anyone who will maintain and extend qlib**. It turns the
chain from "change code → verify → commit → push → watch CI" into steps that can
be followed, and records the pitfalls this project has already hit.

It is an **operational manual**, not a design document. For the rationale behind
the design see [Design decisions](technical/design-decisions.md); for the details
of the build system see [Build system](technical/build-system.md).

> This is the English counterpart of the [Chinese guide](zh/development-workflow.md).
> The two are maintained together: a change to one is a change to the other, and
> neither is a translation afterthought.

---

## 1. Project status at a glance

| Item | Current value |
|---|---|
| Version | `0.1.0` (`SOVERSION 0`) |
| Language standard | **Exactly C11, compiler extensions disabled** (`-std=c11`, not `gnu11`) |
| Modules | `ql_common` (errors/version), `ql_mem` (allocator), `ql_str` (string), `ql_hash` (hash table), `ql_list` (linked list), `ql_vector` (dynamic array), `ql_net` (IPv4 addresses, optional) |
| Public headers | `ql_common.h`, `ql_mem.h`, `ql_str.h`, `ql_hash.h`, `ql_list.h`, `ql_vector.h`, `ql_net.h`, `qlib.h` (umbrella) |
| Unit tests | **106** (93 with the network module off) |
| Fuzz targets | 6 (`fuzz_str`, `fuzz_hash`, `fuzz_list`, `fuzz_vector`, `fuzz_mem`, `fuzz_net`) |
| Benchmark cases | 16 |
| Local verification | `./ci/check.sh`, **11 steps** |
| CI | GitHub Actions, **9 job definitions → 11 jobs per run** (`linux` and `windows` are matrices) |
| Remotes | `origin` = Gitee (primary), `github` = GitHub mirror |

CI badge:

```markdown
[![CI](https://github.com/GalenQiu/qlib/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/GalenQiu/qlib/actions/workflows/ci.yml)
```

---

## 2. Environment setup

Required:

- A C11 compiler (GCC / Clang / AppleClang / MSVC)
- CMake ≥ 3.16
- Python 3 (link checking, Chinese/English consistency checking, installing tools in CI)
- git

Strongly recommended; without them, local verification skips the corresponding
step:

| Tool | Purpose | Consequence if missing |
|---|---|---|
| `clang-format` | Formatting gate | `format` step SKIP |
| `clang-tidy` | Static analysis | `tidy` step SKIP |
| `doxygen` + `graphviz` | API reference | `docs` step SKIP |
| `clang` | Fuzzing (`-fsanitize=fuzzer`) | `fuzz` step SKIP |
| `pkg-config` | Consumer verification | `package` step tests only `find_package` |

`./ci/check.sh` prints the version of every tool at the start. **SKIP is not a
pass** — seeing SKIP means that step was not verified.

---

## 3. The daily development loop

### 3.1 Confirm the starting point is clean

```bash
git status                 # expected: clean
git pull origin main       # if there are other changes on Gitee
```

### 3.2 Change the code

Before writing code, read through the rules in [CONTRIBUTING.md](../CONTRIBUTING.md).
The three most frequently violated rules:

- Public functions must be marked `QLIB_EXPORT`
- Mark `QLIB_NODISCARD` where the return value must not be ignored
- Array allocation must go through `ql_alloc_array` / `ql_realloc_array`; do not
  compute the product yourself

### 3.3 Local verification (the core step)

```bash
./ci/check.sh --quick      # quick: about 5 seconds, skips sanitizers and fuzzing
./ci/check.sh              # full: about 40 seconds
```

The output looks like this:

```
step_links     PASS
step_i18n      PASS
step_format    PASS
step_win32     PASS
step_build     PASS
step_net_off   PASS
step_tidy      PASS
step_package   PASS
step_docs      PASS
step_sanitizers SKIP skipped by --quick
step_fuzz      SKIP skipped by --quick
9 passed, 0 failed, 2 skipped
```

On failure it prints the tail of that step's log directly and summarizes on the
last line:

```
failed: step_format
```

The log is left in `build-check/<step name>.log`.

**Run `--quick` at least once before committing.** If the change touches
allocation, memory layout, parsing, or the build system, run the full version.

### 3.4 Commit

Commit message convention: `[module] short description`, for example:

```
[mem] implement ql_aligned_alloc / ql_aligned_free
[net] implement the optional network module: IPv4 address parsing
[ci] fix the two jobs the first real CI run failed
```

Write the body to explain **why**, not what changed (`git diff` already says what
changed). The commit history of this project records the reasoning behind several
decisions, and that is worth preserving.

### 3.5 Push and watch CI

```bash
git push origin main       # Gitee
git push github main       # GitHub (CI runs here)
```

**CI runs only on GitHub.** Gitee uses Gitee Go, which has different syntax and is
not configured in this repository.

Wait about 4–5 minutes, then:

```bash
# look at the most recent run
curl -sS "https://api.github.com/repos/GalenQiu/qlib/actions/runs?per_page=1" \
  | python3 -c "import json,sys;d=json.load(sys.stdin)['workflow_runs'][0];print(d['status'],d['conclusion'],d['html_url'])"

# look at each job
curl -sS "https://api.github.com/repos/GalenQiu/qlib/actions/runs/<run_id>/jobs" \
  | python3 -c "
import json,sys
for j in json.load(sys.stdin)['jobs']:
    print(f\"{j['name']:36} {j['conclusion']}\")
    for s in j['steps']:
        if s['conclusion']=='failure': print('   FAILED:', s['name'])
"
```

Or open `https://github.com/GalenQiu/qlib/actions` directly.

> The unauthenticated GitHub API allows **only 60 requests per hour**, which is
> easy to exhaust — writing this document exhausted it once. Do not write scripts
> that poll more often than every 20 seconds; 40 polls of one run use up the
> quota. Configure a token (`Authorization: Bearer <token>`) if you need more.

**If you only want to know whether it is green, use the badge, not the API.** The
badge goes through `github.com` rather than `api.github.com` and is not subject to
the quota:

```bash
curl -sSL "https://github.com/GalenQiu/qlib/actions/workflows/ci.yml/badge.svg?branch=main" \
  | grep -oE 'passing|failing'
```

### 3.6 What to do when CI is red

1. **Reproduce locally first**: `./ci/check.sh`. It now covers most of CI,
   including installation and consumer verification.
2. Open the log of the failing job and find the failing step.
3. Use the table below to locate it.

| Failing job | Reproducible locally? | Notes |
|---|---|---|
| `linux / gcc`, `linux / clang` | ✅ `step_build` | Compiler version differs from local; may be a new warning |
| `sanitizers` | ✅ `step_sanitizers` | |
| `fuzz` | ✅ `step_fuzz` | |
| `format` | ✅ `step_format` | CI pins clang-format 23.1.1; the local version may differ |
| `tidy` | ✅ `step_tidy` | |
| `docs` | ✅ `step_docs` | CI pins Doxygen 1.18.0 |
| `install / find_package + pkg-config` | ✅ `step_package` | |
| `windows / msvc` | ❌ only the stub check in `step_win32` | Real MSVC can only be verified by CI |
| `macos / clang` | ❌ | AppleClang + `NET=OFF` configuration |

**If everything is green locally but CI is red, first suspect that local
verification and CI are not verifying the same thing.** This really happened once
in this project; see section 8.3.

---

## 4. Adding a module: the complete checklist

Taking a new `ql_foo` as the example. This checklist is the path `ql_vector` and
`ql_net` actually took.

### 4.1 Write the public header

`include/qlib/ql_foo.h`:

- It must begin with an `@file` documentation block. **Without it, Doxygen
  generates no documentation page for the header**; its functions appear in the
  search index but are displayed nowhere (this pitfall was hit; see 8.4)
- Include guard, `#include "qlib/qlib_export.h"`, `extern "C"` wrapper
- Mark every public function `QLIB_EXPORT`
- Mark those that should be `QLIB_NODISCARD` (whose return value must not be
  ignored)
- Container types are opaque; value-semantics structs must carry a trailing
  `reserved[]`
- **Every struct member needs a documentation comment**, otherwise the `docs` step
  fails on a warning

### 4.2 Write the implementation

`src/ql_foo.c`:

- `#include "ql_internal.h"` is required to use `ql_err_raise()`
- All memory goes through the caller's `ql_allocator_t`; do not call `malloc`
  directly
- Error paths must call `ql_err_raise()` and return an error code

### 4.3 Wire it into the build

Add `src/ql_foo.c` to the `QLIB_SRC` list in `CMakeLists.txt`.

**If the module is optional** (like `QLIB_BUILD_NET`), the "Optional modules"
section of [Build system](technical/build-system.md) lists the four places that
must change together: the source list, the `PUBLIC` compile definition, the header
installation rule, and `Cflags` in `qlib.pc`. Miss one and you get "the header is
shipped but the symbols do not exist".

### 4.4 Add it to the umbrella header

`include/qlib/qlib.h`, one line (optional modules wrapped in `#ifdef`).

### 4.5 Write tests

- Create `tests/test_foo.c`; **the first include is `"test_suites.h"`**
- End the file with `MU_TEST_SUITE(test_suite_foo) { ... }`
- Add `MU_DECLARE_SUITE(test_suite_foo);` to `tests/test_suites.h`
- Add `MU_RUN_SUITE(test_suite_foo);` to `tests/test_main.c`
- Add `test_foo.c` to the source list in `tests/CMakeLists.txt` (added
  conditionally for optional modules)

Choosing an assertion: **use `MU_ASSERT` for a check immediately followed by a
pointer dereference, and `MU_CHECK` for everything else**. `MU_CHECK` continues
executing after a failure, so `MU_CHECK(p != NULL); p[0] = 1;` will segfault
instead of failing cleanly.

### 4.6 Add fuzzing (if the module parses or transforms input)

`fuzz/fuzz_foo.c`, `int LLVMFuzzerTestOneInput(const uint8_t*, size_t)`,
**forward-declare the prototype** (otherwise `-Wmissing-prototypes` errors), add
the target name to the `QLIB_FUZZ_TARGETS` list in `fuzz/CMakeLists.txt`.

Check invariants for real. `fuzz_net.c` uses the round-trip invariant (anything
that parses must format back to an equivalent address), and `fuzz_vector.c` checks
`size <= capacity` and that slots are addressable after every step.

### 4.7 Add benchmarks (if the cost matters to users)

`bench/bench_foo.c` defines `ql_bench_suite_foo`, `bench/bench.h` declares it,
`bench/bench_main.c` registers it in `g_suites[]`, and `bench/CMakeLists.txt` adds
the source file.

Each case must check the result it measures (`QL_BENCH_CHECK`) — **timing broken
code is worse than not timing it at all**. Put preparation data in the `setup`
hook, otherwise the time spent building the input is charged to the measured
operation.

### 4.8 Write documentation

Add `docs/user/foo.md` and `docs/zh/user/foo.md`, and register both in the two
`index.md` files and in `mainpage.md`.

### 4.9 Verify

```bash
./ci/check.sh
```

---

## 5. Documentation maintenance

### 5.1 Chinese and English must stay in sync

English documentation lives in `docs/`, Chinese in `docs/zh/`, **one file for one
file**. Change one side and you must change the other.

`ci/check_i18n.py` checks two things:

1. **The file set**: every English page has a Chinese counterpart (exception:
   `mainpage.md` may exist only once)
2. **Identifiers**: every `ql_*` / `QLIB_*` name is compared **page by page**. A
   function name mentioned on a page in English must also appear on that page in
   Chinese — **appearing on another page does not count**, because that does not
   help someone reading this page

When translating:

- Identifiers, paths, commands, compiler options, and tool names **are always kept
  in English**
- Copy code blocks verbatim; translate only the comments inside them
- If you change an English page, update the Chinese page **in the same commit**,
  or note in the document that the translation is out of date

There are only two built-in exceptions, both in `ci/check_i18n.py` with their
reasons stated: `mainpage.md` (English only, because Doxygen accepts a single landing page).

### 5.2 Code examples in the documentation must compile

Examples in `docs/user/*.md` are copied directly by readers. **This pitfall was
hit**: the example in `vector.md` ignored the return value of `QLIB_NODISCARD` and
produced a warning when copied verbatim.

When writing examples:

- Check the return value of every call that can fail
- Actually run the claimed output to confirm it (the outputs in `net.md` and
  `vector.md` were verified byte for byte)
- Mark snippets that appear only descriptively explicitly as "fragments"

### 5.3 Generate the reference

```bash
cmake -S . -B build -DQLIB_BUILD_DOCS=ON
cmake --build build --target docs      # → docs/html/index.html
```

In CI, **any warning makes this step fail** (Doxygen pinned at 1.18.0).

---

## 6. Releasing a version

The version number lives in two places:

| Location | Effect |
|---|---|
| `project(qlib VERSION 0.1.0)` in `CMakeLists.txt` | Determines `SOVERSION` and `qlibConfigVersion.cmake` |
| `QLIB_VERSION_*` in `include/qlib/ql_common.h` | Compiled into the headers for run-time comparison |

**Both must be changed together**, otherwise `ql_version()` and the header macros
disagree, and a consumer's version check will misjudge.

Release checklist:

1. Update the version in both places
2. `./ci/check.sh` fully green
3. Extend `ql_ret_t`? (**append only at the end**, never reorder)
4. Did the layout of `ql_allocator_t` change? (if so, it is a major version change)
5. Did the signature, semantics, or ownership convention of any public function
   change? (same as above)
6. Tag and push to both remotes:

```bash
git tag -a v0.1.0 -m "qlib 0.1.0"
git push origin v0.1.0 && git push github v0.1.0
```

For the complete list of ABI promises see
[ABI and versioning](technical/abi-and-versioning.md). **Strictly speaking, during
0.x any version may break compatibility**, and the `SameMajorVersion` check cannot
prevent it, so the checklist above is not a formality.

---

## 7. Command cheat sheet

```bash
# local verification
./ci/check.sh                     # all 11 steps
./ci/check.sh --quick             # skip sanitizers and fuzzing
./ci/check.sh --fuzz-seconds=60   # raise the fuzzing budget

# run a single step
python3 ci/check_links.py         # relative links
python3 ci/check_i18n.py          # Chinese/English consistency
clang-format -i src/*.c include/qlib/*.h tests/*.c tests/*.h fuzz/*.c bench/*.c bench/*.h

# build separately
cmake -S . -B build -DQLIB_WERROR=ON -DQLIB_BUILD_BENCH=ON
cmake --build build && ctest --test-dir build --output-on-failure
./build/bin/qlib_bench            # benchmarks
./build/bin/qlib_bench --quick

# various configurations
cmake -S . -B build -DQLIB_BUILD_NET=OFF          # turn off the network module
cmake -S . -B build -DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
CC=clang cmake -S . -B build -DQLIB_BUILD_FUZZ=ON

# push
git push origin main && git push github main
```

---

## 8. Pitfalls we have hit

This section is where this project has actually paid a price. **Worth skimming
before adding code.**

### 8.1 Test harness: `static` counters across translation units

**Symptom**: an assertion failed, yet the test binary printed
`0 tests run, 0 failures` and returned 0; CI stayed green forever.

**Cause**: `minunit.h` declared the counters as `static int mu_failures = 0;`. A
`static` in a header gives **every `.c` file its own copy** — the increment in
`test_common.c` touched its own copy, while `test_main.c` read a different,
all-zero one.

**Current state**: the counters are now `extern`, with a single definition in
`minunit.c`. In addition, a "test that is supposed to fail" probe in
`tests/selftest/` plus a ctest driver guard it, so a regression turns ctest red.

### 8.2 Tests may have no teeth

**Practice**: after writing a test, **deliberately break the implementation** and
confirm the test goes red. This is mutation testing.

The mutations recorded in this project: removing the overflow check, removing the
`memset` in `calloc`, removing the `NOMEM` report, bypassing the alias guard,
removing the alignment slack, removing the power-of-two validation, storing the
wrong pointer in the header — every one was caught.

**The practice itself has also caught bugs in tests**: one test did
`MU_CHECK(state.message != NULL)` followed by `strlen(state.message)`, so a failed
assertion **segfaulted** instead of failing cleanly. That is why `MU_ASSERT`
exists.

### 8.3 Two test fixtures = one too many

**Symptom**: the CI `package` job went red while local `./ci/check.sh` was fully
green.

**Cause**: the workflow **generated a consumer program inline**, while the local
reproduction hand-wrote a different one. When `ql_allocator_t` moved from
`ql_common.h` to `ql_mem.h`, the workflow's copy stopped compiling while the local
copy kept working — **the fork therefore lived silently for several commits**.

**Current state**: the consumer is checked in as `ci/consumer/`, and the workflow
and `ci/check.sh` build **the same file**.

**Lesson**: **a local reproduction must use the same input and the same command as
CI.** Otherwise you are verifying "my equivalent runs", not "the one in CI runs".

### 8.4 Doxygen keys Markdown pages by file name

**Symptom**: a documentation page did not exist at all in the generated reference,
and links to it became unresolved `\ref` warnings.

**Cause**: Doxygen uses the **file name** as the page label. With a `README.md` at
the repository root and another in `docs/`, the two collapsed onto the same label,
and only the root one was published.

**Current state**: `docs/index.md` and `docs/zh/index.md` (not README.md). The
price is that a code host no longer renders the page automatically when browsing
the directory.

### 8.5 No `@file`, no documentation page

**Symptom**: Doxygen reported 0 warnings and everything looked fine, but **the
generated reference contained not a single API function**.

**Cause**: with `EXTRACT_ALL = NO`, a header must have an `@file` documentation
block for a page to be generated. At the time only `qlib.h` had one; the functions
in the other three headers were indexed but displayed nowhere — **"no page"
produced no warning**.

**Lesson**: **"0 warnings" does not mean "the right thing was produced".** Adding
`@file` immediately exposed four undocumented macros and one missing `@param`.

### 8.6 `configure_file` substitutes `@NAME@` inside comments

**Symptom**: comments in the generated configuration file were mangled.

**Cause**: CMake's `configure_file(@ONLY)` substitutes **every** `@name@`,
comments included. An undefined name becomes an empty string, so the comment is
silently eaten.

**Practice**: **do not write at-sign-wrapped tokens in comments** in template
files.

### 8.7 `grep` on a missing file = a false pass

**Symptom**: a gate "passed" while checking nothing at all.

**Cause**: `grep` returns non-zero when the file does not exist. So
`if grep -q "warning:" missing.log; then fail; fi` takes the else branch and
prints "no warnings".

**This mistake was made twice** — once grepping a log file that a cmake target
never wrote (so "0 warnings" was measured against a nonexistent file), and once in
a script where `grep` found no match and execution continued.

**Practice**: `test -f` before reading a file; either handle `grep`'s exit status
explicitly, or use `if ... then` instead of relying on it to decide "no problem".

### 8.8 `target_compile_features` is only a floor

**Symptom**: CONTRIBUTING claims "C11, no compiler extensions", but the code was
actually compiled as C23 with all extensions enabled.

**Cause**: `target_compile_features(c_std_11)` expresses a **floor**. When the
compiler's default dialect already satisfies it, CMake **adds no `-std=` flag at
all**. GCC 16 defaults to C23, so the code was compiled as C23.

**Current state**: `CMAKE_C_STANDARD` + `C_STANDARD_REQUIRED` + `C_EXTENSIONS OFF`
together actually emit `-std=c11`.

**Lesson**: **to verify that build flags really take effect, look at
`compile_commands.json`**; do not assume.

### 8.9 Fuzzing may find the harness's own bugs first

The first version of `fuzz_mem` reported `out-of-memory` after 57000 runs. The
investigation showed that **the harness was wrong**: the "invalid alignment"
branch passed the raw 32-bit value straight through, and one of them happened to be
`0x80000000` — **a genuine power of two**. The library correctly accepted it and,
per its documented contract, tried to allocate a block of one alignment's size
(2 GiB).

**Lesson**: when fuzzing reports a problem, **first determine whether it is a
defect in the library or in the harness**; do not rush to change the library.

### 8.10 `.clang-format` conflicts with the code style

`BreakBeforeBraces: Attach` conflicts with the Allman style used throughout the
repository: running `clang-format -i` once, as CONTRIBUTING says, rewrites every
function signature. Two related pitfalls:

- `BraceWrapping`'s per-item overrides **only take effect under
  `BreakBeforeBraces: Custom`**; any other value is dead configuration
- Once the braces of `extern "C"` are wrapped, without
  `IndentExternBlock: NoIndent` the **entire body of the header is indented one
  level**

### 8.11 ASCII-armored apt signing keys must be dearmored

**Symptom**: in CI, `apt-get update` reports the repository as unsigned and the
tools cannot be installed.

**Cause**: an armored key was put directly into the `signed-by` keyring. apt's
verifier (gpgv) **does not dearmor automatically**, so it cannot read it.

**Practice**: `curl ... | sudo gpg --dearmor -o /usr/share/keyrings/xxx.gpg`.
Locally you can verify with `gpgv --keyring`: an armored key reports
`invalid packet`, a dearmored one reports `Good signature`.

### 8.12 `set -euo pipefail` and a grep that finds nothing

Under `set -euo pipefail`, `grep -E "error|warning" | head -3` returns non-zero
overall when **there is no match**, so `set -e` aborts the script — **it looks like
"the build failed" when the build actually succeeded** (there were no errors to
match).

**Practice**: append `|| true` to commands like this, or drop `set -e` and handle
the exit status explicitly.

### 8.13 Always restore files after experiments

After mutating source with `sed` / `python` for mutation testing, restore it with
`git checkout -- <file>` and confirm that `git status` is clean. Do not rely on a
backup in `/tmp` — in some sandbox environments `/tmp` is **per-command**, and the
backup disappears.

---

## 9. Tool version pinning policy

Three tools are pinned in CI, because their output changes between versions and
**a promise like "no new warnings" is only meaningful against a pinned version**:

| Tool | CI version | Source | Why pinned |
|---|---|---|---|
| clang-format | 23.1.1 | PyPI | Formatting output changes between versions |
| clang-tidy | 23.x | apt.llvm.org | The check set and warning text change |
| Doxygen | 1.18.0 | GitHub Release tarball | Warning output changes |

**You do not need the same versions locally**, but know this: a local format check
passing while the CI format check fails is usually a version difference.

`./ci/check.sh` deliberately does **not** pin versions; it uses whatever is
installed on the machine — it is a convenience, not a substitute for CI.
