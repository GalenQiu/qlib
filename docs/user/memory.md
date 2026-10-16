# Memory

qlib never calls `malloc` on your behalf. Every allocation goes through a
`ql_allocator_t` you supply, and passing `NULL` means "use the default
libc-backed allocator".

## The allocator

```c
typedef struct {
    ql_malloc_fn  malloc;
    ql_realloc_fn realloc;
    ql_free_fn    free;
    void*         userdata;
    void*         reserved[4];
} ql_allocator_t;
```

The three callbacks all receive `userdata` as their final argument, which is
how a custom allocator reaches its own state:

```c
typedef struct {
    size_t live_blocks;
    size_t peak_bytes;
} my_state;

static void* my_malloc(size_t size, void* userdata)
{
    my_state* state = userdata;
    void* p = malloc(size);
    if (p != NULL) {
        state->live_blocks++;
    }
    return p;
}

static void* my_realloc(void* ptr, size_t size, void* userdata)
{
    (void)userdata;
    return realloc(ptr, size);
}

static void my_free(void* ptr, void* userdata)
{
    my_state* state = userdata;
    if (ptr != NULL) {
        state->live_blocks--;
    }
    free(ptr);
}

my_state state = {0, 0};
ql_allocator_t alloc = {
    .malloc = my_malloc,
    .realloc = my_realloc,
    .free = my_free,
    .userdata = &state,
};
```

Rules to respect:

- **All three function pointers must be non-NULL.** Pass
  `ql_default_allocator()` if you only want to override some of the behaviour.
- `reserved` must be zeroed. Designated initializers as above do that for you.
  Those slots exist so the struct can grow without an ABI break; do not use
  them.
- The callbacks must follow the C semantics of their libc namesakes.
  `realloc(ptr, 0)` is not required to behave in any particular way by qlib,
  because qlib never issues it.

`ql_default_allocator()` returns a fully populated allocator backed by libc:

```c
ql_allocator_t alloc = ql_default_allocator();
```

## Allocators are copied, not referenced

`ql_vector_create()` copies the `ql_allocator_t` into the vector. The caller's
struct does not have to outlive the container:

```c
ql_vector_t* make_vector(void)
{
    ql_allocator_t alloc = ql_default_allocator();   /* stack local, fine */
    ql_vector_t* vec = NULL;
    if (ql_vector_create(&alloc, sizeof(int), &vec) != QL_OK) {
        return NULL;
    }
    return vec;    /* vec owns its own copy of `alloc` */
}
```

`userdata` is copied by value too, so if it points at something, that something
must still be alive when the container is used.

## The allocation helpers

`<qlib/ql_mem.h>` provides the building blocks. All of them accept `NULL` for
the allocator, meaning the default one.

| Function | Notes |
|---|---|
| `ql_alloc(alloc, size)` | `size == 0` returns `NULL` and is not an error |
| `ql_calloc(alloc, count, size)` | Overflow checked, then zero filled |
| `ql_realloc(alloc, ptr, size)` | `size == 0` frees `ptr` and returns `NULL` |
| `ql_alloc_array(alloc, count, size)` | `count * size`, overflow checked |
| `ql_realloc_array(alloc, ptr, count, size)` | As above, for a resize |
| `ql_dealloc(alloc, ptr)` | `NULL` is a no-op |
| `ql_size_mul_overflow(count, size, &result)` | The check on its own |

```c
#include <qlib/qlib.h>

int* values = ql_alloc_array(NULL, 16, sizeof(int));
if (values == NULL) {
    /* either out of memory, or 16 * sizeof(int) overflowed */
}

/* Growing keeps the old contents; on failure the original is untouched. */
int* bigger = ql_realloc_array(NULL, values, 32, sizeof(int));
if (bigger == NULL) {
    ql_dealloc(NULL, values);   /* values is still valid and still yours */
} else {
    values = bigger;
    ql_dealloc(NULL, values);
}
```

All of these are marked `QLIB_NODISCARD`, so discarding a result produces a
compiler warning. Ignoring an allocation result is nearly always a bug.

## Overflow checking

`count * size` is the classic integer-overflow mistake. `ql_alloc_array()` and
`ql_realloc_array()` cannot make it: they reject the request with
`QL_ERR_OVERFLOW` instead of computing a wrapped size and allocating a buffer
that is too small.

```c
size_t total = 0;
if (ql_size_mul_overflow(count, elem_size, &total) != QL_OK) {
    /* count * elem_size does not fit in size_t */
}
```

`ql_size_mul_overflow()` returns `QL_ERR_INVALID_ARG` if `result` is `NULL`,
and `QL_ERR_OVERFLOW` for an overflowing product. On failure `result` is left
unmodified. A `count` of 0 can never overflow.

Use the array helpers rather than computing the product yourself; this is one
of the project's contribution rules.

## Failure reporting

When an allocation fails, the helper returns `NULL` **and** raises
`QL_ERR_NOMEM` through the [error handler](errors.md). An overflowing array
request raises `QL_ERR_OVERFLOW`. The return value is what you branch on; the
handler is there so a central place can observe the failure.

## Ownership summary

| Thing | Who frees it |
|---|---|
| Memory from `ql_alloc`/`ql_calloc`/`ql_alloc_array`/`ql_realloc*` | You, with `ql_dealloc`, using the same allocator |
| A container from `ql_vector_create` | You, with `ql_vector_destroy`, which releases the buffer and the control block |
| Pointers from `ql_vector_at`/`ql_vector_data` | Nobody; they point into the container's buffer and are invalidated by mutation |
| The allocator struct you pass in | You; qlib copies it and never keeps your pointer |

## Thread safety

The allocator and the helpers hold no global state, apart from the error
handler, so they are safe to use from several threads as long as each
container is confined to one thread. Containers are not internally
synchronized, and there is no locking module: nothing in qlib is concurrent
yet, so nothing claims to be.
