# Vector

`ql_vector_t` is a growable, contiguous array of fixed size elements. It is an
opaque type: you only ever hold a pointer, and the layout is private to the
library.

```c
#include <qlib/qlib.h>

ql_vector_t* vec = NULL;
if (ql_vector_create(NULL, sizeof(int), &vec) != QL_OK) {
    return -1;
}
/* ... */
ql_vector_destroy(vec);
```

## Creating and destroying

```c
ql_ret_t ql_vector_create(const ql_allocator_t* alloc, size_t elem_size, ql_vector_t** out);
void     ql_vector_destroy(ql_vector_t* vec);
```

`elem_size` is the size in bytes of one element and must be non-zero. The
allocator is copied into the vector, so the struct you pass need not outlive
the call; `NULL` selects the default allocator.

On failure `ql_vector_create()` returns a code and sets `*out` to `NULL`, so a
failed create never leaves a dangling pointer behind. `ql_vector_destroy(NULL)`
is a no-op, which makes cleanup paths simpler.

## Adding and removing

| Function | Behaviour |
|---|---|
| `ql_vector_push_back(vec, elem)` | Appends a copy. Grows when full |
| `ql_vector_pop_back(vec, out)` | Removes the last element into `out`; `out` may be `NULL` to discard. `QL_ERR_EMPTY` when empty |
| `ql_vector_insert(vec, index, elem)` | Inserts so the copy ends up at `index`; `index == size` appends |
| `ql_vector_remove(vec, index)` | Removes at `index`, shifting the rest down |
| `ql_vector_assign(vec, index, elem)` | Overwrites the element at `index` |
| `ql_vector_clear(vec)` | Drops all elements, keeping the capacity |

```c
int i;
for (i = 0; i < 100; i++) {
    if (ql_vector_push_back(vec, &i) != QL_OK) {
        return -1;
    }
}

int last = 0;
if (ql_vector_pop_back(vec, &last) == QL_OK) {
    /* last == 99 */
}
```

`insert` accepts `index == size` (append) but rejects anything larger with
`QL_ERR_OUT_OF_RANGE`. `remove` and `assign` require `index < size`.

## Reading

```c
void*  ql_vector_at(ql_vector_t* vec, size_t index);   /* NULL if out of range */
void*  ql_vector_data(ql_vector_t* vec);               /* NULL when empty */
size_t ql_vector_size(const ql_vector_t* vec);
size_t ql_vector_capacity(const ql_vector_t* vec);
size_t ql_vector_elem_size(const ql_vector_t* vec);
bool   ql_vector_empty(const ql_vector_t* vec);
```

The accessors return `void*`, so cast before dereferencing:

```c
int* slot = ql_vector_at(vec, 3);
if (slot != NULL) {
    printf("%d\n", *slot);
} else {
    /* index 3 is past the end, or vec is NULL */
}
```

The queries are safe on a `NULL` vector: they report 0, `NULL`, or empty. That
is deliberate, so destructor-like and logging code does not need a NULL check
before every call.

## Capacity and growth

| Function | Behaviour |
|---|---|
| `ql_vector_reserve(vec, capacity)` | Ensures room for at least `capacity`. Never shrinks |
| `ql_vector_resize(vec, count)` | Sets the element count |

```c
ql_vector_reserve(vec, 1000);   /* one allocation instead of many */
for (i = 0; i < 1000; i++) {
    ql_vector_push_back(vec, &i);   /* no reallocation happens */
}
```

Capacity starts at 0 and grows geometrically, doubling from an initial 8, so
`push_back` is amortized O(1). Reserve ahead of a known loop count when the
cost of the intermediate reallocations matters.

`ql_vector_resize()` growing zero-fills the newly exposed elements. Shrinking
only lowers the count; the bytes stay in the buffer until they are overwritten,
so do not treat a shrink as a secure erase.

Both functions reject a size that cannot be represented as `count * elem_size`
with `QL_ERR_OVERFLOW`, and on any failure the vector is left exactly as it
was.

## Element types

Elements are copied bytewise with `memcpy`, so an element type must be
trivially copyable: no internal pointers that need deep copying, no
constructors.

```c
typedef struct {
    int    id;
    double x, y;
} point;

ql_vector_t* points = NULL;
ql_vector_create(NULL, sizeof(point), &points);

point p = {1, 2.0, 3.0};
ql_vector_push_back(points, &p);
```

For a vector of `char*` the vector stores the pointers, not the strings; you
own whatever they point at.

## Pointer invalidation

Pointers from `ql_vector_at()` and `ql_vector_data()` point into the vector's
internal buffer. They are invalidated by:

- any operation that grows the capacity: `push_back`, `insert`, `reserve`,
  `resize` upward
- `insert` and `remove` at or before the element, which shift data

They survive `pop_back`, `assign`, `clear`, and writes to other elements.

```c
int* a = ql_vector_at(vec, 0);
ql_vector_push_back(vec, &value);   /* a is now dangling */
a = ql_vector_at(vec, 0);           /* re-fetch before using it again */
```

This is the one rule that most often bites, and it is the same rule
`std::vector` has in C++.

## Aliasing is handled

Passing an element that points into the vector's own buffer is supported and
does the right thing:

```c
/* Duplicate the first element onto the end. */
if (ql_vector_push_back(vec, ql_vector_at(vec, 0)) != QL_OK) { /* handle */ }

/* Copy element 3 to the front, shifting the rest. */
if (ql_vector_insert(vec, 0, ql_vector_at(vec, 3)) != QL_OK) { /* handle */ }

/* Self-assignment, which must use memmove semantics, not memcpy. */
if (ql_vector_assign(vec, 0, ql_vector_at(vec, 0)) != QL_OK) { /* handle */ }
```

The `if (...)` wrappers are not decoration: every mutating vector function is
marked `QLIB_NODISCARD`, so ignoring the result is a compiler warning. The
snippets in this document that appear to ignore results are fragments meant to
be read, not copied verbatim.

Internally the element is copied to a temporary before the buffer is touched.
Without that, growing the buffer would realloc the very bytes being read. You
do not need to work around it, and you do not need to copy the element out
yourself.

## Error codes by function

| Situation | Code | Raised by |
|---|---|---|
| `vec` or `elem` is `NULL` | `QL_ERR_INVALID_ARG` | every function taking them |
| `elem_size == 0` | `QL_ERR_INVALID_ARG` | `ql_vector_create` |
| `out == NULL` | `QL_ERR_INVALID_ARG` | `ql_vector_create` |
| `index > size` | `QL_ERR_OUT_OF_RANGE` | `ql_vector_insert` |
| `index >= size` | `QL_ERR_OUT_OF_RANGE` | `ql_vector_remove`, `ql_vector_assign` |
| `size == 0` | `QL_ERR_EMPTY` | `ql_vector_pop_back` |
| `capacity * elem_size` overflows | `QL_ERR_OVERFLOW` | `reserve`, `resize`, `push_back`, `insert` |
| allocation fails | `QL_ERR_NOMEM` | `reserve`, `resize`, `push_back`, `insert` |

Every one of these is also reported to the installed
[error handler](errors.md), with a message naming the function.

## A worked example

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_vector_t* vec = NULL;
    size_t i;

    if (ql_vector_create(NULL, sizeof(int), &vec) != QL_OK) {
        return 1;
    }

    /* Reserve once so the loop below never reallocates. */
    if (ql_vector_reserve(vec, 8) != QL_OK) {
        ql_vector_destroy(vec);
        return 1;
    }

    for (i = 0; i < 8; i++) {
        int value = (int)i * 10;
        if (ql_vector_push_back(vec, &value) != QL_OK) {
            ql_vector_destroy(vec);
            return 1;
        }
    }
    /* vec == {0, 10, 20, 30, 40, 50, 60, 70}, size == capacity == 8 */

    /* Duplicate the last element; this forces a grow from 8 to 16. */
    if (ql_vector_push_back(vec, ql_vector_at(vec, 7)) != QL_OK) {
        ql_vector_destroy(vec);
        return 1;
    }

    /* Drop the first two. Both are fallible, so both are checked: every
     * mutating ql_vector function is marked QLIB_NODISCARD. */
    if (ql_vector_remove(vec, 0) != QL_OK || ql_vector_remove(vec, 0) != QL_OK) {
        ql_vector_destroy(vec);
        return 1;
    }

    printf("size %zu, capacity %zu\n", ql_vector_size(vec), ql_vector_capacity(vec));
    for (i = 0; i < ql_vector_size(vec); i++) {
        printf("%d ", *(int*)ql_vector_at(vec, i));
    }
    printf("\n");

    ql_vector_destroy(vec);
    return 0;
}
```

Output:

```
size 7, capacity 16
20 30 40 50 60 70 70
```
