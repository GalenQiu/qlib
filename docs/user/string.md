# Strings

`<qlib/ql_str.h>` provides `ql_str_t`: a growable byte buffer that is always
NUL terminated. It is the string equivalent of `ql_vector` — the same allocator
rules, the same "check the return code" discipline — with two extra guarantees
that make it pleasant to build text with:

- `ql_str_cstr()` always returns a valid C string, even when the string is
  empty, so the result goes straight into `printf`, `fopen` or any C API.
- Every mutating call either succeeds or leaves the previous contents untouched.
  There is no state where the length and the buffer disagree.

## Building a string

```c
ql_str_t* text = NULL;

if (ql_str_create(NULL, &text) != QL_OK) {
    /* out of memory */
}
if (ql_str_append(text, "Hello") != QL_OK ||
    ql_str_append_char(text, ' ') != QL_OK ||
    ql_str_append_printf(text, "%s", "world") != QL_OK) {
    ql_str_destroy(text);
    /* handle the failure */
}
```

The available mutators:

| Function | Effect |
|---|---|
| `ql_str_assign` / `ql_str_assign_n` | Replace the contents |
| `ql_str_append` | Append a C string |
| `ql_str_append_n` | Append a byte range, NUL bytes included |
| `ql_str_append_char` | Append one byte |
| `ql_str_append_str` | Append another `ql_str_t` |
| `ql_str_append_printf` / `ql_str_append_vprintf` | Append formatted text |
| `ql_str_clear` | Make it empty, keeping the buffer |
| `ql_str_reserve` | Grow the buffer ahead of time; never shrinks |

`ql_str_append_printf` grows and retries rather than truncating, so a format that
expands to something large is handled. It applies the format to a copy of the
`va_list`, so a failed attempt does not consume the caller's.

## Reading it back

```c
printf("%s\n", ql_str_cstr(text));   /* "Hello world" */
printf("%zu bytes\n", ql_str_size(text));
```

`ql_str_cstr()` never returns `NULL` for a valid string; an empty string yields
`""`. `ql_str_size()` is the authoritative length and does not call `strlen`.

## Embedded NUL bytes

`ql_str_assign_n()` and `ql_str_append_n()` accept data containing NUL bytes.
After that the length is larger than `strlen()` reports:

```c
static const char data[] = {'a', 'b', '\0', 'c', 'd'};

ql_str_assign_n(text, data, sizeof data);
printf("%zu\n", ql_str_size(text));          /* 5 */
printf("%zu\n", strlen(ql_str_cstr(text)));  /* 2  */
```

Nothing breaks, but anything treating the result as a C string stops at the
first NUL. `ql_str_equal_cstr()` compares lengths first, so a string holding an
embedded NUL never equals a C string of the same visible prefix.

If you never append NUL bytes, none of this matters.

## Comparing and searching

```c
if (ql_str_compare(a, b) == 0) { /* equal */ }
if (ql_str_equal_cstr(a, "expected")) { /* ... */ }

size_t index;
if (ql_str_find(a, "needle", &index) == QL_OK) {
    printf("found at %zu\n", index);
}
```

`ql_str_compare()` orders byte by byte over the shorter length and treats the
shorter string as the lesser one — the same order as `strcmp` for strings
without embedded NULs, and a total order in all cases.

`ql_str_find()` reports `QL_ERR_NOT_FOUND` rather than an error when the needle
is absent, and leaves the output untouched in that case. An empty needle matches
at index 0, as `strstr` does.

## Appending a string to itself

```c
ql_str_append_str(text, text);       /* duplicates the contents */
ql_str_append(text, ql_str_cstr(text));  /* the same thing */
```

Both are supported. The buffer may move during growth, so the source is
re-derived from an offset afterwards. This is the one place where the
implementation looks up whether a pointer belongs to its own buffer.

## A complete example

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_str_t* greeting = NULL;

    if (ql_str_create_from(NULL, "Hello", &greeting) != QL_OK) {
        return 1;
    }
    if (ql_str_append(greeting, ", ") != QL_OK ||
        ql_str_append_printf(greeting, "%s!", "world") != QL_OK) {
        ql_str_destroy(greeting);
        return 1;
    }

    printf("%s (%zu bytes)\n", ql_str_cstr(greeting), ql_str_size(greeting));

    ql_str_destroy(greeting);
    return 0;
}
```

```
Hello, world! (13 bytes)
```

## Performance

Measured on the development machine by `bin/qlib_bench`:

| Operation | Cost |
|---|---|
| `ql_str_append_char` (amortised) | 11 ns |
| `ql_str_append_printf` | 31 ns |

Appending a character is amortised O(1): the buffer doubles. `printf` is
dominated by `vsnprintf`, not by the string.

## Error codes

| Situation | Code |
|---|---|
| `NULL` string, source, format or output | `QL_ERR_INVALID_ARG` |
| Allocation failed | `QL_ERR_NOMEM` |
| Length or capacity would overflow `size_t` | `QL_ERR_OVERFLOW` |
| `ql_str_find()` did not find the needle | `QL_ERR_NOT_FOUND` |

Every one is also reported to the installed [error handler](errors.md), except
`QL_ERR_NOT_FOUND` from `ql_str_find()`, which is an ordinary answer rather than
a fault.
