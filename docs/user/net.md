# Network addresses

`<qlib/ql_net.h>` turns textual IPv4 addresses into values and back. It is the
first slice of the network module, and deliberately the slice that needs no
sockets, no platform initialisation and no external library: it is pure
computation over a string, so it works the same on every platform qlib supports
and needs no network to test.

Sockets, DNS and the socket-library startup Windows requires are not here yet.

> The module is optional. A build configured with `QLIB_BUILD_NET=OFF` omits it
> entirely, and then `<qlib/ql_net.h>` is not installed either. Including
> `<qlib/qlib.h>` works in both configurations. See
> [integration](integration.md).

## The address type

```c
typedef enum {
    QL_NET_AF_UNSPEC = 0,   /* no address; the zero value */
    QL_NET_AF_INET,         /* IPv4 */
    QL_NET_AF_INET6         /* IPv6: reserved, not implemented yet */
} ql_net_family_t;

typedef struct {
    ql_net_family_t family;
    unsigned char   bytes[16];   /* IPv4 uses bytes[0..3] */
    void*           reserved[2];
} ql_net_addr_t;
```

qlib uses its own family values rather than the platform's `AF_INET` and
friends, which differ between Windows and POSIX; exposing those would leak the
difference into every caller.

The type is small, has no invariants beyond the family tag, and callers need to
build one on the stack, so it is one of the exposed value types the project
allows. It stores sixteen bytes even though IPv4 needs four, which is what makes
adding IPv6 later a non-breaking change.

A zeroed `ql_net_addr_t` is a valid value: family `QL_NET_AF_UNSPEC`, meaning
"no address".

## Parsing

```c
#include <qlib/qlib.h>

ql_net_addr_t addr;
if (ql_net_addr_parse("192.168.1.1", &addr) != QL_OK) {
    /* not an address */
}
```

The accepted form is exactly four decimal octets separated by dots, each 0 to
255, with nothing before, after or between them:

| Input | Result |
|---|---|
| `0.0.0.0`, `255.255.255.255`, `10.20.30.40` | `QL_OK` |
| `1.2.3` , `1.2.3.4.5`, `1..3.4`, `1.2.3.4 ` | `QL_ERR_INVALID_ARG` |
| `1.2.3.256`, `999.1.1.1` | `QL_ERR_OUT_OF_RANGE` |
| `::1`, `2001:db8::1` | `QL_ERR_NOT_SUPPORTED` |
| `01.2.3.4`, `010.0.0.1` | `QL_ERR_INVALID_ARG` |

Two of those rows deserve a word.

**Octets above 255 are `QL_ERR_OUT_OF_RANGE`, not a syntax error.** That
distinguishes "you wrote something that is not an address" from "you wrote an
address whose octet cannot be represented", which is usually the more useful
distinction when logging a bad configuration file.

**Leading zeros are rejected rather than guessed at.** `010` is ten in decimal
and eight in octal, and every real-world security bug of that shape comes from a
parser picking the wrong one. Write `10.0.0.1`. A lone `0` is unambiguous and
stays legal.

**IPv6 is `QL_ERR_NOT_SUPPORTED`.** The text is recognised as IPv6 rather than
reported as a malformed IPv4 address, so a caller can tell "not yet" from
"wrong".

On success the whole destination is written, including the unused bytes and
`reserved`, so the result is deterministic whatever the caller's variable held
before.

## Formatting

```c
char buf[QL_NET_ADDR_STR_MAX];
if (ql_net_addr_format(&addr, buf, sizeof buf) != QL_OK) {
    /* buffer too small, or not an IPv4 address */
}
puts(buf);   /* "192.168.1.1" */
```

`QL_NET_ADDR_STR_MAX` is 46, sized for IPv6 as well, so a buffer that size never
needs revisiting. IPv4 needs only 16.

Use a caller-supplied buffer rather than a returned static: it keeps the
function thread-safe and makes the lifetime obvious.

The buffer is left **untouched** on failure, so a caller cannot mistake a
partial write for a result. A buffer smaller than the address needs is
`QL_ERR_OUT_OF_RANGE`; formatting `QL_NET_AF_UNSPEC` is `QL_ERR_INVALID_ARG`
because "no address" has no text; formatting `QL_NET_AF_INET6` is
`QL_ERR_NOT_SUPPORTED`.

## Comparing

```c
if (ql_net_addr_equal(&a, &b)) {
    /* same family and the same address bytes */
}
```

Only the bytes the family actually uses are compared, so padding never affects
the result. Family is part of identity: the same bytes tagged IPv4 and IPv6 are
not equal. `NULL` is not equal to anything, not even another `NULL`.

## A complete example

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_net_addr_t addr;
    char text[QL_NET_ADDR_STR_MAX];

    if (ql_net_addr_parse("203.0.113.7", &addr) != QL_OK) {
        return 1;
    }
    if (ql_net_addr_format(&addr, text, sizeof text) != QL_OK) {
        return 1;
    }
    printf("%s family=%d\n", text, (int)addr.family);

    if (ql_net_addr_equal(&addr, &addr)) {
        puts("equal to itself");
    }
    return 0;
}
```

```
203.0.113.7 family=1
equal to itself
```

## Error codes

| Situation | Code |
|---|---|
| `NULL` argument | `QL_ERR_INVALID_ARG` |
| Malformed text: wrong octet count, empty octet, non-digit, trailing characters, leading zero | `QL_ERR_INVALID_ARG` |
| Octet above 255 | `QL_ERR_OUT_OF_RANGE` |
| Formatting an address of family `QL_NET_AF_UNSPEC` | `QL_ERR_INVALID_ARG` |
| Formatting an address of an unknown family | `QL_ERR_INVALID_ARG` |
| Buffer smaller than the address needs | `QL_ERR_OUT_OF_RANGE` |
| Text is IPv6, or the address is IPv6 | `QL_ERR_NOT_SUPPORTED` |

Every one of these is also reported to the installed
[error handler](errors.md), with a message naming the function.
