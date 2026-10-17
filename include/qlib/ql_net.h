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
 * The module has two halves. Address handling is pure computation: it needs no
 * network, no platform initialisation and no library to link. TCP sockets need
 * all three -- Windows has to be told to start Winsock, and the socket library
 * has to be linked -- so they sit behind QLIB_BUILD_NET and the address half
 * keeps working without them.
 *
 * Address handling covers both families. Sockets do not: they are IPv4 only,
 * and passing an IPv6 address to one reports QL_ERR_NOT_SUPPORTED rather than
 * silently doing something else. The split is deliberate -- IPv6 sockets are a
 * separate piece of platform work -- and the address half keeps working
 * regardless.
 *
 * Sockets here are blocking, and there is no DNS: build a ql_net_addr_t with
 * ql_net_addr_parse() and connect to it. See
 * docs/technical/design-decisions.md for why.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qlib/ql_common.h"
#include "qlib/ql_mem.h"

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
    QL_NET_AF_INET6,      /**< IPv6. Parsed and formatted; not connectable. */
} ql_net_family_t;

/**
 * @brief An opaque TCP socket.
 *
 * Created by ql_net_tcp_listen() or ql_net_tcp_connect(), and closed with
 * ql_net_socket_close(). Accepted connections are created by ql_net_accept()
 * and are independent of the listener that produced them.
 */
typedef struct ql_net_socket ql_net_socket_t;

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
 * IPv6 is recognised by the presence of a colon. The accepted forms are the
 * eight-group form, `::` compression (at most once), and a trailing dotted quad
 * such as `::ffff:192.0.2.1`. Zone identifiers (`%eth0`) are rejected.
 *
 * @return QL_OK; QL_ERR_INVALID_ARG for a NULL argument or malformed text;
 *         QL_ERR_OUT_OF_RANGE if an IPv4 octet is above 255.
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
 * IPv6 is rendered the way RFC 5952 asks: lowercase, no leading zeros, the
 * longest run of two or more zero groups compressed with `::` (leftmost on a
 * tie), and an IPv4-mapped address in the mixed notation (`::ffff:192.0.2.1`).
 *
 * @return QL_OK; QL_ERR_INVALID_ARG for a NULL argument or an address of
 *         family QL_NET_AF_UNSPEC; QL_ERR_OUT_OF_RANGE if @p size is too small
 *         (16 bytes for IPv4, QL_NET_ADDR_STR_MAX for IPv6).
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

/**
 * @brief Resolve a host name to an IPv4 address.
 *
 * Uses the platform resolver (`getaddrinfo`), so `/etc/hosts`, the DNS
 * configuration and the platform's caching all apply. A numeric address is
 * accepted and returned without a lookup, which is also the only form that
 * works with no network at all.
 *
 * Only IPv4 results are considered; a host with only AAAA records is reported
 * as not found rather than silently connecting over a family this module does
 * not implement.
 *
 * @param host Host name or numeric IPv4 address. Must not be NULL or empty.
 * @param out  Receives the first IPv4 address found.
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_NOT_FOUND when the name does not
 *         resolve, QL_ERR_AGAIN when the resolver is temporarily unable,
 *         QL_ERR_NOMEM, or QL_ERR_SYS.
 */
QLIB_EXPORT ql_ret_t ql_net_addr_resolve(const char* host, ql_net_addr_t* out);

/**
 * @brief Prepare the platform's socket layer.
 *
 * Windows requires `WSAStartup()` before any socket call; POSIX has no
 * equivalent. Every socket function below calls this itself, so calling it
 * explicitly is optional -- but doing so at startup surfaces a platform failure
 * at a point the caller controls, instead of on the first connection.
 *
 * Idempotent. @return QL_OK, or QL_ERR_SYS if the platform refused to start.
 */
QLIB_EXPORT ql_ret_t ql_net_init(void);

/**
 * @brief Tear the platform's socket layer down.
 *
 * Optional: the operating system reclaims everything at exit. Call it before a
 * deliberate re-initialisation, or from a test that wants a clean slate. Any
 * socket still open at that point becomes unusable, so close them first.
 */
QLIB_EXPORT void ql_net_shutdown(void);

/**
 * @brief Listen for TCP connections on an address and port.
 *
 * @param alloc   Allocator, or NULL for the default one.
 * @param addr    Local address to bind. Must be QL_NET_AF_INET.
 * @param port    Port in host byte order. Pass 0 to let the system choose one,
 *                then read it back with ql_net_local_port().
 * @param backlog Pending connection queue length. Values below 1 become 1.
 * @param out     Receives the listening socket. Set to NULL before any failure.
 * Sockets are IPv4 only: an IPv6 address is QL_ERR_NOT_SUPPORTED.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_NOMEM, QL_ERR_NOT_SUPPORTED for
 *         IPv6, or QL_ERR_IO / QL_ERR_SYS from the platform.
 *
 * `SO_REUSEADDR` is set, so a server restarted immediately after a shutdown is
 * not refused by the TIME_WAIT of its previous connections.
 */
QLIB_EXPORT ql_ret_t ql_net_tcp_listen(const ql_allocator_t* alloc, const ql_net_addr_t* addr, uint16_t port,
                                       int backlog, ql_net_socket_t** out);

/**
 * @brief Connect to a TCP server.
 *
 * Blocking: returns once the connection is established or failed. There is no
 * DNS, so @p addr must already be an address.
 *
 * Sockets are IPv4 only: an IPv6 address is QL_ERR_NOT_SUPPORTED.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_NOMEM, QL_ERR_SYS when the platform
 *         refused to start or the address family is unsupported, or QL_ERR_IO
 *         when the peer refused or is unreachable.
 */
QLIB_EXPORT ql_ret_t ql_net_tcp_connect(const ql_allocator_t* alloc, const ql_net_addr_t* addr, uint16_t port,
                                        ql_net_socket_t** out);

/**
 * @brief Accept the next pending connection.
 *
 * Blocking: returns once a connection arrives. Use ql_net_set_timeout() on the
 * listener to bound the wait.
 *
 * The accepted socket inherits the listener's allocator. @return QL_OK,
 * QL_ERR_INVALID_ARG, QL_ERR_NOMEM, QL_ERR_AGAIN or QL_ERR_TIMEOUT if nothing
 * arrived in time, or QL_ERR_IO / QL_ERR_SYS from the platform.
 */
QLIB_EXPORT ql_ret_t ql_net_accept(const ql_net_socket_t* listener, ql_net_socket_t** out);

/**
 * @brief Send bytes.
 *
 * A short send is normal and not an error: @p out_sent receives how many bytes
 * the kernel accepted, which may be fewer than @p len. Call again with the rest.
 *
 * @param sock     Open connection.
 * @param data     Bytes to send. Must not be NULL unless @p len is 0.
 * @param len      How many bytes to send.
 * @param out_sent Receives the byte count the kernel accepted.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_AGAIN if the send buffer is full,
 *         or QL_ERR_IO / QL_ERR_SYS from the platform.
 */
QLIB_EXPORT ql_ret_t ql_net_send(ql_net_socket_t* sock, const void* data, size_t len, size_t* out_sent);

/**
 * @brief Receive bytes.
 *
 * @param sock         Open connection.
 * @param buf          Destination buffer. Must not be NULL unless @p cap is 0.
 * @param cap          Size of @p buf.
 * @param out_received Receives the byte count. **Zero means the peer closed the
 *                     connection cleanly** -- that is the POSIX convention and
 *                     it is not an error.
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_TIMEOUT if nothing arrived within
 *         the timeout, or QL_ERR_IO / QL_ERR_SYS.
 */
QLIB_EXPORT ql_ret_t ql_net_recv(ql_net_socket_t* sock, void* buf, size_t cap, size_t* out_received);

/**
 * @brief Bound how long a blocking call may wait, in milliseconds.
 *
 * Applies to receive and send. 0 means "wait forever", which is the default.
 *
 * The wait is implemented with `select()` rather than by setting
 * `SO_RCVTIMEO`, so an expired timeout is QL_ERR_TIMEOUT on every platform.
 * Leaving it to `SO_RCVTIMEO` would mean QL_ERR_AGAIN on Linux and macOS (where
 * it cannot be told apart from a full buffer) and WSAETIMEDOUT on Windows, and
 * on Windows it does not reliably interrupt `accept()` at all.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_SYS if the platform rejected it.
 */
QLIB_EXPORT ql_ret_t ql_net_set_timeout(ql_net_socket_t* sock, int millis);

/**
 * @brief The local port a socket is bound to.
 *
 * The reason this exists: pass port 0 to ql_net_tcp_listen() and read back what
 * the system chose.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_SYS.
 */
QLIB_EXPORT ql_ret_t ql_net_local_port(const ql_net_socket_t* sock, uint16_t* out_port);

/**
 * @brief Close a socket and free it.
 *
 * Passing NULL is a no-op, so a close can follow a failed create.
 */
QLIB_EXPORT void ql_net_socket_close(ql_net_socket_t* sock);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_NET_H */
