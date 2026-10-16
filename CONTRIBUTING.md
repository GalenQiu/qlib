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
Use clang-format.
```bash
clang-format -i src/*.c include/qlib/*.h tests/*.c tests/*.h
```

## Static check

clang-tidy before commit.

## Test

- Add unit test for every new feature / bugfix.
- Run tests with ASAN.
```
cmake -DCMAKE_C_FLAGS="-fsanitize=address -g" ..
make
./bin/qlib_test
```

## Commit

Commit message: `[module] short description`.
Example: `[common] implement default allocator`.

