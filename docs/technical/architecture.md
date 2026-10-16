# Architecture

## Directory layout

| Path | Role |
|---|---|
| [`include/qlib/`](../../include/qlib) | Public headers. This is the only directory installed. |
| [`src/`](../../src) | Library implementation. |
| [`tests/`](../../tests) | Unit test harness and test suites. |
| [`fuzz/`](../../fuzz) | libFuzzer targets. |
| [`cmake/`](../../cmake) | Templates consumed by `configure_file()` and `configure_package_config_file()`. |
| [`docs/`](../../docs) | User guide, technical documentation, and the Doxygen configuration. |
| `bench/`, `ci/`, `examples/` | Placeholders; each holds only `.gitkeep`. |

`include/qlib/qlib_export.h` is **generated** into the build tree by `generate_export_header()`, not present in the source tree. See [build system](build-system.md).

## Layering

```text
                 qlib/qlib.h  (umbrella header)
                       |
     +-----------------+-----------------+
     |                 |                 |
ql_common.h       ql_mem.h         ql_vector.h
     |                 |                 |
     |                 +-------> ql_common.h
     |                                   |
     +-----------------------------------+
                       |
                 src/ql_internal.h      (private, not installed)
                       |
   +-------------------+--------------------+
   |                   |                    |
ql_common.c         ql_mem.c           ql_vector.c
```

Dependencies point in one direction only: `ql_vector` depends on `ql_mem`, `ql_mem` depends on `ql_common`, and `ql_common` depends on nothing but the C library.

| Module | Owns | Does not own |
|---|---|---|
| `ql_common` | `ql_ret_t`, `ql_strerror()`, `ql_ret_from_errno()`, the error-handler registry (`ql_set_error_handler()` / `ql_get_error_handler()`), version macros and `ql_version()`, `QLIB_NODISCARD` | Any allocation. `src/ql_common.c` includes the private header and `<errno.h>`, and no allocator. |
| `ql_mem` | `ql_allocator_t` and its function-pointer types, `ql_default_allocator()`, the allocation helpers (`ql_alloc`, `ql_calloc`, `ql_realloc`, `ql_dealloc`, `ql_alloc_array`, `ql_realloc_array`), `ql_size_mul_overflow()` | Error codes themselves; it calls into `ql_common` to raise. |
| `ql_vector` | `ql_vector_t` and the `struct ql_vector` layout, growth policy, element copy/shift logic | Allocation strategy; every byte goes through the allocator stored in the vector. |

Two rules keep the layering intact:

1. A module may include headers of modules below it, never above it.
2. Every allocation funnels through the `ql_allocator_t` stored in the object. There is no direct `malloc()` outside `src/ql_mem.c` (see [CONTRIBUTING.md](../../CONTRIBUTING.md) rule 7).

## Public header graph

Each public header is self-contained: including it alone is enough to use its declarations.

| Header | Includes | Provides |
|---|---|---|
| [`ql_common.h`](../../include/qlib/ql_common.h) | `<stddef.h>`, `qlib/qlib_export.h` | version, `ql_ret_t`, error handler |
| [`ql_mem.h`](../../include/qlib/ql_mem.h) | `<stddef.h>`, `ql_common.h` | `ql_allocator_t` and allocation helpers |
| [`ql_vector.h`](../../include/qlib/ql_vector.h) | `<stdbool.h>`, `<stddef.h>`, `ql_common.h`, `ql_mem.h` | `ql_vector_t` |
| [`qlib.h`](../../include/qlib/qlib.h) | all three above | umbrella convenience header |

`ql_vector.h` includes `ql_mem.h` rather than forward-declaring `ql_allocator_t`, because several of its functions take a `const ql_allocator_t*` and callers need the complete struct to build one. Every header wraps its declarations in `extern "C"` guards for C++ consumers.

## The private boundary

[`src/ql_internal.h`](../../src/ql_internal.h) lives in `src/`, is never installed (`install(DIRECTORY include/qlib ...)` only copies the public tree), and declares the one cross-module helper:

```c
void ql_err_raise(ql_ret_t code, const char* message);
```

`ql_err_raise()` is defined in `src/ql_common.c` and called from `src/ql_mem.c` and `src/ql_vector.c`, so it must have external linkage at link time. It is deliberately **not** marked `QLIB_EXPORT`, which has different consequences per platform:

| Platform | Effect |
|---|---|
| Windows / PE | Without `__declspec(dllexport)` the symbol is not placed in the DLL export table or the import library, so a consumer cannot call it. Inside the DLL the call resolves normally. |
| ELF | No visibility preset is set (`C_VISIBILITY_PRESET` is not configured), so the symbol is still present in the dynamic symbol table. `ql_err_raise` is therefore reachable by accident on Linux/macOS even though it is not part of the public API. |

So "not exported" is a Windows-enforced contract and an ELF convention. Treating `ql_err_raise()` as private means: it is not covered by the version rules in [ABI and versioning](abi-and-versioning.md), and it may change or disappear in any release.

## Where opaque types sit

`ql_vector_t` is an incomplete type in the public header; `struct ql_vector` is defined only in `src/ql_vector.c`:

```c
/* ql_vector.h — visible to callers */
typedef struct ql_vector ql_vector_t;

/* ql_vector.c — hidden */
struct ql_vector
{
    ql_allocator_t alloc;
    void* data;
    size_t size;
    size_t capacity;
    size_t elem_size;
};
```

Callers hold `ql_vector_t*` and can only reach the contents through accessors. The struct can gain, lose or reorder fields without breaking anyone who compiled against the header, because no caller ever computes `sizeof(struct ql_vector)` or a member offset.

`ql_allocator_t` is the deliberate exception: it is a complete struct in [`ql_mem.h`](../../include/qlib/ql_mem.h), because users must be able to construct one on the stack and pass it by value. It carries `void* reserved[4]` to preserve the same freedom to grow. See [design decisions](design-decisions.md) for the reasoning.

## Request flow: `ql_vector_push_back()`

The path below is the template for every mutating operation in the library.

```c
ql_ret_t ql_vector_push_back(ql_vector_t* vec, const void* elem);
```

| # | Step | Location | Failure behaviour |
|---|---|---|---|
| 1 | Reject `vec == NULL` or `elem == NULL` | `src/ql_vector.c` | `ql_err_raise(QL_ERR_INVALID_ARG, ...)`, return `QL_ERR_INVALID_ARG` |
| 2 | Ask `ql_vector_pin_elem()` whether `elem` points into the vector's own live buffer | `ql_vector_contains()` | — |
| 3 | If it does alias, allocate `elem_size` bytes as scratch and copy the element there | `ql_alloc()` | Return `QL_ERR_NOMEM`; scratch freed |
| 4 | If `size == capacity`, grow | `ql_vector_grow()` | Return `QL_ERR_OVERFLOW` or `QL_ERR_NOMEM`; vector unchanged |
| 5 | Compute the new capacity (double, checked) and the byte count | `ql_size_mul_overflow()` | `QL_ERR_OVERFLOW` if `capacity * elem_size` does not fit in `size_t` |
| 6 | `ql_realloc(&vec->alloc, vec->data, bytes)` | `src/ql_mem.c` | On NULL, `ql_err_raise(QL_ERR_NOMEM, ...)` and return; `vec->data` and `vec->capacity` are left untouched, so the existing elements survive |
| 7 | Resolve the allocator: `alloc != NULL ? alloc : &ql_default_alloc` | `ql_alloc_resolve()` | — |
| 8 | Invoke `resolved->realloc(ptr, size, resolved->userdata)` | **user callback** | Whatever the user's allocator returns |
| 9 | `memcpy()` the pinned element into slot `size`, then `size++` | `src/ql_vector.c` | — |
| 10 | Free the scratch copy if one was made | `ql_dealloc()` | — |

Steps 2–3 are what make `ql_vector_push_back(vec, ql_vector_at(vec, 0))` safe: without them, step 6 could hand back a different buffer while `elem` still points into the freed one. The reasoning, and the ASAN evidence that the guard is load-bearing, are in [design decisions](design-decisions.md).

Step 6 also shows the single place where the library touches platform memory management: `ql_default_allocator()` returns a `ql_allocator_t` whose function pointers wrap libc `malloc`/`realloc`/`free`, and every layer above it only ever sees the callback table.
