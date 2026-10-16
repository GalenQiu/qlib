# qlib Contributing Guide

## Coding Rules
1. C11 standard, no compiler extensions.
2. Public types are opaque (`struct xxx` defined only in `.c`). The only
   exceptions are the small value-semantics callback tables (`ql_allocator_t`,
   `ql_lock_t`), which users must be able to construct on the stack. Any
   exported struct must carry trailing `reserved[]` fields so it can grow
   without breaking ABI.
3. All public API prefix: `ql_{module}_{action}`.
4. Return `ql_ret_t` for mutable functions.
5. Check NULL input pointers, return `QL_ERR_INVALID_ARG`.
6. `assert()` only for internal invariant checks, not user input validation.
7. Memory must go through `ql_allocator_t`, never direct malloc/realloc/free.
8. Platform specific code guarded by `_WIN32` / `__linux__`.

## Code format

Use clang-format **23.1.1**. CI enforces this exact version, because formatting
output differs between releases.
```bash
clang-format -i src/*.c include/qlib/*.h tests/*.c tests/*.h
```

## Static check

The rules live in `.clang-tidy`, with `tests/.clang-tidy` disabling two checks
that misfire on the assertion macros. Any finding is an error. Run clang-tidy
**23.1**:
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy src/*.c tests/*.c -p build
```

## Test

- Add a unit test for every new feature and every bugfix.
- Declare each new suite in `tests/test_suites.h` and run it from
  `tests/test_main.c`.
- Run the suite under sanitizers:
```bash
cmake -S . -B build -DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```
- `qlib_harness_selftest` contains an assertion that is *supposed* to fail: it
  proves a failing test actually fails the run. Do not "fix" it.

## Commit

Commit message: `[module] short description`.
Example: `[common] implement default allocator`.

