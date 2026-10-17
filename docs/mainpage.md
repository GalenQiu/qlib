# qlib

qlib is a general purpose C11 library: data structures, algorithms, network
utilities and common tools, intended to be usable as-is in production code.
It targets Linux, macOS and Windows, and is released under the MIT licence.

The library is built around three ideas:

- **Opaque types.** Public types are incomplete types; their layout lives in
  the `.c` file. Callers cannot depend on struct layout, so the library can fix
  and extend internals without breaking ABI.
- **Replaceable memory.** Nothing allocates behind the caller's back. Every
  allocation goes through a `ql_allocator_t` the caller supplies, and a `NULL`
  allocator means the default libc-backed one.
- **Explicit return codes.** Fallible functions return `ql_ret_t`, and every
  code has a stable human readable string via `ql_strerror()`.

## Modules

| Module | Header | Contents |
|---|---|---|
| Common | `ql_common.h` | version macros, `ql_ret_t`, `ql_strerror()`, `ql_ret_from_errno()`, the error handler |
| Memory | `ql_mem.h` | `ql_allocator_t`, `ql_default_allocator()`, overflow checked allocation helpers |
| Vector | `ql_vector.h` | `ql_vector_t`, a growable contiguous array |
| String | `ql_str.h` | `ql_str_t`, a growable, always NUL terminated byte string |
| Hash table | `ql_hash.h` | `ql_hash_t`, fixed size keys to fixed size values |
| Linked list | `ql_list.h` | `ql_list_t`, doubly linked with stable node handles |
| Threads | `ql_thread.h` | threads, mutexes and condition variables (optional module) |
| Logging | `ql_log.h` | levels, plain/raw/JSON formats, console and rotating-file sinks (optional module) |
| Network | `ql_net.h` | IPv4 address handling and TCP sockets (optional module) |
| Umbrella | `qlib.h` | includes all of the above |

## Where to read more

User documentation:

- [Getting started](user/getting-started.md)
- [Integration](user/integration.md)
- [Error handling](user/errors.md)
- [Memory](user/memory.md)
- [Vector](user/vector.md)
- [Strings](user/string.md)
- [Hash table](user/hash.md)
- [Linked list](user/list.md)
- [Threads](user/thread.md)
- [Logging](user/log.md)
- [Network addresses](user/net.md)

Technical documentation:

- [Architecture](technical/architecture.md)
- [Design decisions](technical/design-decisions.md)
- [ABI and versioning](technical/abi-and-versioning.md)
- [Portability](technical/portability.md)
- [Build system](technical/build-system.md)
- [Testing](technical/testing.md)

All of the above are also available in Simplified Chinese under
[`zh/`](zh/index.md) (中文文档).

## A first program

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_vector_t* vec = NULL;
    int i;

    /* NULL means "use the default allocator". */
    if (ql_vector_create(NULL, sizeof(int), &vec) != QL_OK) {
        return 1;
    }
    for (i = 0; i < 10; i++) {
        if (ql_vector_push_back(vec, &i) != QL_OK) {
            ql_vector_destroy(vec);
            return 1;
        }
    }
    printf("%zu elements, capacity %zu\n", ql_vector_size(vec), ql_vector_capacity(vec));
    ql_vector_destroy(vec);
    return 0;
}
```
