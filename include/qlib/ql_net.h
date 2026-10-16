#ifndef QLIB_QL_NET_H
#define QLIB_QL_NET_H

/**
 * @file ql_net.h
 * @brief Network address handling: parsing, formatting and comparing textual
 *        addresses.
 *
 * This is the first slice of the network module, and deliberately the slice
 * that needs no sockets, no platform initialisation and no external library:
 * it is pure computation over a string. That keeps QLIB_BUILD_NET a
 * source-level toggle rather than something that drags in a link dependency,
 * and it makes every code path testable without a network.
 *
 * Only IPv4 is implemented. IPv6 is reported as QL_ERR_NOT_SUPPORTED rather
 * than as a syntax error, and ql_net_addr_t already stores enough bytes for it,
 * so adding it later is not an ABI change.
 *
 * Sockets, DNS and the socket-library initialisation that Windows requires are
 * deliberately absent; see docs/technical/design-decisions.md.
 */

#include <stdbool.h>
#include <stddef.h>

#include "qlib/ql_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Address families, using qlib's own values rather than the platform's.
 *
 * The platform's AF_INET and friends differ between Windows and POSIX, and
 * exposing them would leak that difference into every caller.
 */
typedef enum
{
    QL_NET_AF_UNSPEC = 0, /**< No address; the zero value. */
    QL_NET_AF_INET,       /**< IPv4. */
    QL_NET_AF_INET6,      /**< IPv6. Reserved: not parsed or formatted yet. */
} ql_net_family_t;

/**
 * @brief Bytes needed for the longest textual address, terminator included.
 *
 * Sized for IPv6, so a buffer of this size never needs revisiting.
 */
#define QL_NET_ADDR_STR_MAX 46

/**
 * @brief A network address, held by value.
 *
 * This is one of the exposed value types the project allows: it is small, has
 * no invariants beyond the family tag, and callers need to build one on the
 * stack. The trailing reserved members are zeroed by ql_net_addr_parse() and
 * must be left alone.
 */
typedef struct
{
    /** Which family @c bytes holds. QL_NET_AF_UNSPEC means "no address". */
    ql_net_family_t family;
    /** Raw address bytes. IPv4 uses bytes[0..3]; bytes[4..15] stay zero. */
    unsigned char bytes[16];
    /** Reserved for future growth. Must be zeroed; do not use. */
    void* reserved[2];
} ql_net_addr_t;

/**
 * @brief Parse a textual IPv4 address in dotted-quad form.
 *
 * Leading zeros in an octet are rejected rather than guessed at, since
 * "010.0.0.1" is ambiguous between decimal and octal; write "10.0.0.1".
 * Surrounding whitespace is not accepted.
 *
 * @param text NUL terminated text, for example @c "192.168.1.1".
 * @param out  Receives the parsed address. Zeroed before parsing, so on success
 *             it is fully deterministic including @c reserved.
 * @return QL_OK; QL_ERR_INVALID_ARG for a NULL argument or malformed text;
 *         QL_ERR_OUT_OF_RANGE if an octet is above 255;
 *         QL_ERR_NOT_SUPPORTED if @p text looks like IPv6.
 */
QLIB_EXPORT ql_ret_t ql_net_addr_parse(const char* text, ql_net_addr_t* out);

/**
 * @brief Write an address in dotted-quad form.
 *
 * @param addr Address to format; must be QL_NET_AF_INET.
 * @param buf  Destination buffer. On failure it is left untouched, so a caller
 *             cannot mistake a partial write for a result.
 * @param size Size of @p buf in bytes. Needs at least 16 for IPv4, and
 *             QL_NET_ADDR_STR_MAX covers every family.
 * @return QL_OK; QL_ERR_INVALID_ARG for a NULL argument or an address of
 *         family QL_NET_AF_UNSPEC; QL_ERR_OUT_OF_RANGE if @p size is too small;
 *         QL_ERR_NOT_SUPPORTED for IPv6.
 */
QLIB_EXPORT ql_ret_t ql_net_addr_format(const ql_net_addr_t* addr, char* buf, size_t size);

/**
 * @brief Compare two addresses for equality.
 *
 * Two NULL pointers are not equal to anything, not even each other; an address
 * only ever equals another address of the same family and the same bytes.
 * Bytes beyond the family's length are not compared.
 */
QLIB_NODISCARD QLIB_EXPORT bool ql_net_addr_equal(const ql_net_addr_t* a, const ql_net_addr_t* b);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_NET_H */
