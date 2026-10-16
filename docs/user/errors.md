# Error handling

Every fallible function in qlib returns `ql_ret_t`. `QL_OK` is 0, so the
idiomatic check is `if (rc != QL_OK)`. The library never uses `errno` as its
own error channel.

## Return codes

| Code | `ql_strerror()` text | Meaning |
|---|---|---|
| `QL_OK` | `success` | No error |
| `QL_ERR_NOMEM` | `out of memory` | An allocation failed |
| `QL_ERR_INVALID_ARG` | `invalid argument` | NULL pointer, or an argument outside its contract |
| `QL_ERR_OUT_OF_RANGE` | `value out of range` | Representable but outside the valid range |
| `QL_ERR_EMPTY` | `container is empty` | The operation needs a non-empty container |
| `QL_ERR_NOT_FOUND` | `not found` | A lookup found nothing |
| `QL_ERR_IO` | `I/O error` | An I/O operation failed |
| `QL_ERR_SYS` | `system error` | An unmapped platform or OS error |
| `QL_ERR_AGAIN` | `temporarily unavailable` | Retrying may succeed |
| `QL_ERR_TIMEOUT` | `timed out` | A deadline expired |
| `QL_ERR_BUSY` | `resource busy` | The resource is in use |
| `QL_ERR_EXISTS` | `already exists` | The item is already present |
| `QL_ERR_NOT_SUPPORTED` | `not supported` | Not available on this platform or in this build |
| `QL_ERR_OVERFLOW` | `arithmetic overflow` | An arithmetic result would not fit; the request was rejected |

New codes are only ever appended, so a code's numeric value is stable across
releases. Treat the set as open: write `default:` branches rather than assuming
you have enumerated everything. See
[ABI and versioning](../technical/abi-and-versioning.md).

## Turning codes into text

```c
#include <qlib/qlib.h>
#include <stdio.h>

ql_ret_t rc = ql_vector_reserve(vec, 1000);
if (rc != QL_OK) {
    fprintf(stderr, "reserve failed: %s\n", ql_strerror(rc));
    return rc;
}
```

`ql_strerror()` is **total**: it never returns `NULL`, and it returns
`"unknown error"` for a value that is not a known code. That makes it safe to
call on a `ql_ret_t` that arrived from somewhere you do not control, such as a
corrupted struct or an ABI mismatch. It returns a pointer to a static string,
so do not free it and do not expect it to be localized.

## Mapping `errno`

When you have a platform error and want a qlib code:

```c
#include <errno.h>

FILE* f = fopen(path, "rb");
if (f == NULL) {
    ql_ret_t rc = ql_ret_from_errno(errno);
    fprintf(stderr, "%s\n", ql_strerror(rc));   /* e.g. "not found" */
    return rc;
}
```

`0` maps to `QL_OK` and anything unrecognised maps to `QL_ERR_SYS`, so the
function is total in both directions. The mapping covers `ENOMEM`, `EINVAL`,
`ERANGE`, `ENOENT`, `EIO`, `EEXIST`, `EAGAIN`, `ETIMEDOUT`, `EBUSY`,
`EOVERFLOW` and `ENOTSUP` where the platform defines them; the MSVC `errno` set
is narrower than the POSIX one, so several of those do not exist on Windows.

## The error handler

`ql_ret_t` tells the immediate caller what happened. The error handler is for
the case where you want a single place to log or count failures no matter how
deep they originate:

```c
#include <qlib/qlib.h>
#include <stdio.h>

static void on_error(ql_ret_t code, const char* message, void* userdata)
{
    int* count = userdata;
    (*count)++;
    fprintf(stderr, "qlib: %s (%s)\n", message, ql_strerror(code));
}

int failures = 0;
ql_set_error_handler(on_error, &failures);

/* ... use the library; failures now reach on_error as they happen ... */

ql_set_error_handler(NULL, NULL);   /* remove it again */
```

Semantics worth knowing:

- The handler is invoked **at the point of failure**, on the thread that hit it.
- `message` is a static string that gives more context than `ql_strerror()`,
  for example `"ql_alloc: allocation failed"`. It is never `NULL`.
- `userdata` is whatever was passed to `ql_set_error_handler()`.
- **The handler must not call back into the library.** It runs while an
  operation is unwinding, and re-entering could deadlock or corrupt state.
  Keep it to logging, counting, or setting a flag.
- A failure still returns its error code (or `NULL`) to the caller. The handler
  is a notification channel, not a replacement for checking return values.
- Setting the handler is intended to happen during start-up, before other
  threads can read it.

With no handler installed, failures are silent apart from the return value.
That is the default, and it is what makes the library usable in code that
handles errors locally.

## What is checked, and what is not

qlib checks arguments that a caller can plausibly get wrong from outside the
library: NULL pointers, zero element sizes, indices past the end, and
arithmetic that would overflow. Those produce `QL_ERR_INVALID_ARG`,
`QL_ERR_OUT_OF_RANGE` or `QL_ERR_OVERFLOW` rather than undefined behaviour.

It does not re-check internal invariants at run time. Those are covered by
`assert()` in debug builds and by the test suite, per the project's
[contribution rules](../../CONTRIBUTING.md).

## Patterns

Return the code rather than translating it, so the caller keeps the full
information:

```c
static ql_ret_t load_all(ql_vector_t* vec, const char* path)
{
    ql_ret_t rc = read_into(vec, path);
    if (rc != QL_OK) {
        return rc;              /* not QL_ERR_IO: that would lose the cause */
    }
    return ql_vector_reserve(vec, 128);
}
```

When a function can fail for several reasons but the caller does not care
which, `ql_ret_t` still beats a bare `int`, because the caller can pass it
straight to `ql_strerror()` and to the error handler without a translation
table.
