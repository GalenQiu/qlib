/*
 * Ask the C library to expose its POSIX 2008 declarations, which is what
 * getaddrinfo() needs. This is not a compiler extension and does not change the
 * dialect -- the build still passes -std=c11 -- it only decides which
 * declarations the system headers make visible. Windows ignores it entirely.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
/* The name is POSIX's, not ours: the standard requires this exact reserved
 * identifier to select the declaration set. */
// NOLINTNEXTLINE(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp) -- required by POSIX
#  define _POSIX_C_SOURCE 200809L
#endif

#include "qlib/ql_net.h"

#include "ql_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/*
 * Sockets.
 *
 * The platform difference this file has to absorb:
 *
 *   Windows                     POSIX
 *   SOCKET (UINT_PTR),          int,
 *   INVALID_SOCKET,             -1,
 *   closesocket,                close,
 *   WSAGetLastError,            errno,
 *   WSAStartup before any call, nothing to start,
 *   SO_RCVTIMEO takes a DWORD,  takes a struct timeval,
 *   getsockname takes int*,     takes socklen_t*,
 *   setsockopt takes char*,     takes void*.
 *
 * Everything above that is the same calls with the same meanings, so the
 * #ifdefs stay close to the calls they adjust instead of being spread through
 * the logic.
 */
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET ql_net_handle_t;
#  define QL_NET_INVALID_HANDLE INVALID_SOCKET
#  define QL_NET_SOCK_OPT_CAST (const char*)
typedef int ql_net_socklen_t;
/* Windows send()/recv() take and return int; POSIX takes size_t and returns
 * ssize_t. */
typedef int ql_net_io_result_t;
#  define QL_NET_IO_LEN(x) ((int)(x))
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
typedef int ql_net_handle_t;
#  define QL_NET_INVALID_HANDLE (-1)
#  define QL_NET_SOCK_OPT_CAST
typedef socklen_t ql_net_socklen_t;
typedef ssize_t ql_net_io_result_t;
#  define QL_NET_IO_LEN(x) (x)
#endif

/** @brief One TCP socket. Opaque to callers. */
struct ql_net_socket
{
    ql_allocator_t alloc;
    ql_net_handle_t handle;
    /** Milliseconds a blocking wait may take; 0 means forever. */
    int timeout_ms;
};

/** @brief Whether the platform socket layer has been started. */
static bool g_net_started = false;

/* "255.255.255.255" plus the terminator. */
#define QL_NET_IPV4_STR_SIZE 16

/** @brief Value of a hexadecimal digit, or -1. */
static int ql_net_hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/**
 * @brief Parse a dotted quad at @p p.
 *
 * Leading zeros are rejected rather than guessed at, for the reason recorded in
 * ADR-14: "010" is ten in decimal and eight in octal, and every real-world bug
 * of that shape comes from a parser picking one.
 *
 * @param p    Start of the text.
 * @param out  Receives the four octets.
 * @param end  Receives the position just past the quad.
 * @return QL_OK, QL_ERR_INVALID_ARG for malformed text, or
 *         QL_ERR_OUT_OF_RANGE for an octet above 255.
 */
static ql_ret_t ql_net_parse_octets(const char* p, unsigned char out[4], const char** end)
{
    size_t index;

    for (index = 0; index < 4; index++)
    {
        unsigned int value = 0;
        int digits = 0;

        if (index > 0)
        {
            if (*p != '.')
            {
                ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: expected '.' between octets");
                return QL_ERR_INVALID_ARG;
            }
            p++;
        }

        while (*p >= '0' && *p <= '9')
        {
            if (digits == 0 && *p == '0' && p[1] >= '0' && p[1] <= '9')
            {
                ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: octet has a leading zero");
                return QL_ERR_INVALID_ARG;
            }
            value = (value * 10U) + (unsigned int)(*p - '0');
            if (value > 255U)
            {
                ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_net_addr_parse: octet is above 255");
                return QL_ERR_OUT_OF_RANGE;
            }
            digits++;
            p++;
        }

        if (digits == 0)
        {
            ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: octet is empty");
            return QL_ERR_INVALID_ARG;
        }
        out[index] = (unsigned char)value;
    }

    *end = p;
    return QL_OK;
}

/** @brief Parse one hexadecimal group, advancing @p p past it. */
static ql_ret_t ql_net_parse_hex_group(const char** p, unsigned int* out)
{
    unsigned int value = 0;
    int digits = 0;

    while (**p != '\0' && **p != ':')
    {
        int nibble = ql_net_hex_value(**p);

        if (nibble < 0 || ++digits > 4)
        {
            ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: bad hexadecimal group");
            return QL_ERR_INVALID_ARG;
        }
        value = (value << 4U) | (unsigned int)nibble;
        (*p)++;
    }

    if (digits == 0)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: empty group");
        return QL_ERR_INVALID_ARG;
    }

    *out = value;
    return QL_OK;
}

/** @brief Parse an embedded dotted quad, which stands for two groups. */
static ql_ret_t ql_net_parse_embedded_v4(const char* p, unsigned int groups[8], int* count)
{
    unsigned char octets[4];
    const char* end = p;
    ql_ret_t rc = ql_net_parse_octets(p, octets, &end);

    if (rc != QL_OK)
    {
        return rc;
    }
    if (*end != '\0' || *count > 6)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: bad embedded IPv4 address");
        return QL_ERR_INVALID_ARG;
    }

    groups[(*count)++] = ((unsigned int)octets[0] << 8U) | (unsigned int)octets[1];
    groups[(*count)++] = ((unsigned int)octets[2] << 8U) | (unsigned int)octets[3];
    return QL_OK;
}

/**
 * @brief Expand a `::` into zero groups.
 *
 * Moves the parsed tail to the end, leaves zeros in the gap, and returns the
 * number of groups, which is eight on success.
 */
static ql_ret_t ql_net_expand_gap(unsigned int groups[8], int count, int gap)
{
    int tail = count - gap;
    int i;

    /* The compressed run must stand for at least one group. */
    if (tail > 8 - gap)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: '::' compresses nothing");
        return QL_ERR_INVALID_ARG;
    }

    for (i = 0; i < tail; i++)
    {
        groups[7 - i] = groups[count - 1 - i];
    }
    for (i = gap; i < 8 - tail; i++)
    {
        groups[i] = 0;
    }
    return QL_OK;
}

/** @brief Write the eight groups into the address bytes. */
static void ql_net_store_ipv6(unsigned char out[16], const unsigned int groups[8])
{
    int i;

    for (i = 0; i < 8; i++)
    {
        out[(size_t)i * 2] = (unsigned char)(groups[i] >> 8U);
        out[((size_t)i * 2) + 1] = (unsigned char)(groups[i] & 0xFFU);
    }
}

/**
 * @brief Parse an IPv6 address.
 *
 * Accepts the eight-group form, `::` compression (at most once), and a trailing
 * dotted quad such as `::ffff:192.0.2.1`, which contributes the last two
 * groups.
 */
/* The branches here are the IPv6 grammar: eight groups, one optional `::`, and
 * an optional embedded dotted quad. Splitting it further would move the grammar
 * into a helper's parameter list rather than remove it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- the branches are the grammar
static ql_ret_t ql_net_parse_ipv6(const char* text, unsigned char out[16])
{
    unsigned int groups[8];
    int count = 0;
    int gap = -1;
    const char* p = text;

    memset(groups, 0, sizeof groups);

    if (text[0] == ':')
    {
        if (text[1] != ':')
        {
            ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: address starts with a single ':'");
            return QL_ERR_INVALID_ARG;
        }
        gap = 0;
        p = text + 2;
    }

    while (*p != '\0')
    {
        /* A dotted quad may end the address, standing in for two groups. */
        if (strchr(p, '.') != NULL && strchr(p, ':') == NULL)
        {
            ql_ret_t rc = ql_net_parse_embedded_v4(p, groups, &count);

            if (rc != QL_OK)
            {
                return rc;
            }
            break;
        }

        if (count >= 8)
        {
            ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: too many groups");
            return QL_ERR_INVALID_ARG;
        }
        {
            ql_ret_t rc = ql_net_parse_hex_group(&p, &groups[count]);

            if (rc != QL_OK)
            {
                return rc;
            }
            count++;
        }

        if (*p != ':')
        {
            if (*p != '\0')
            {
                ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: unexpected character");
                return QL_ERR_INVALID_ARG;
            }
            break;
        }

        p++;
        if (*p == ':')
        {
            if (gap >= 0)
            {
                ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: '::' appears twice");
                return QL_ERR_INVALID_ARG;
            }
            gap = count;
            p++;
        }
        else if (*p == '\0')
        {
            ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: address ends with ':'");
            return QL_ERR_INVALID_ARG;
        }
    }

    if (gap >= 0)
    {
        ql_ret_t rc = ql_net_expand_gap(groups, count, gap);

        if (rc != QL_OK)
        {
            return rc;
        }
        count = 8;
    }

    if (count != 8)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: IPv6 address needs eight groups");
        return QL_ERR_INVALID_ARG;
    }

    memset(out, 0, 16);
    ql_net_store_ipv6(out, groups);
    return QL_OK;
}

ql_ret_t ql_net_addr_parse(const char* text, ql_net_addr_t* out)
{
    ql_net_addr_t addr;
    ql_ret_t rc;

    if (text == NULL || out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: text or out is NULL");
        return QL_ERR_INVALID_ARG;
    }

    memset(&addr, 0, sizeof addr);

    if (strchr(text, ':') != NULL)
    {
        rc = ql_net_parse_ipv6(text, addr.bytes);
        if (rc != QL_OK)
        {
            return rc;
        }
        addr.family = QL_NET_AF_INET6;
        *out = addr;
        return QL_OK;
    }

    {
        const char* end = text;

        rc = ql_net_parse_octets(text, addr.bytes, &end);
        if (rc != QL_OK)
        {
            return rc;
        }
        if (*end != '\0')
        {
            ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: trailing characters after the address");
            return QL_ERR_INVALID_ARG;
        }
    }

    addr.family = QL_NET_AF_INET;
    *out = addr;
    return QL_OK;
}

/**
 * @brief Render an IPv6 address the way RFC 5952 asks: lowercase, no leading
 *        zeros, and the longest run of two or more zero groups compressed with
 *        `::` (the leftmost run on a tie).
 */
static void ql_net_format_ipv6(const unsigned char bytes[16], char* buf, size_t size)
{
    /* RFC 5952 section 5: an IPv4-mapped address is written in the mixed
     * notation, so ::ffff:192.0.2.1 keeps the dotted quad a human can read
     * instead of becoming ::ffff:c000:201. */
    static const unsigned char mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};

    if (memcmp(bytes, mapped_prefix, sizeof mapped_prefix) == 0)
    {
        (void)snprintf(buf, size, "::ffff:%u.%u.%u.%u", (unsigned)bytes[12], (unsigned)bytes[13], (unsigned)bytes[14],
                       (unsigned)bytes[15]);
        return;
    }

    {
        unsigned int groups[8];
        int best_start = -1;
        int best_length = 0;
        size_t used = 0;
        int i;

        for (i = 0; i < 8; i++)
        {
            groups[i] = ((unsigned int)bytes[(size_t)i * 2] << 8U) | (unsigned int)bytes[((size_t)i * 2) + 1];
        }

        for (i = 0; i < 8;)
        {
            if (groups[i] != 0)
            {
                i++;
                continue;
            }
            {
                int start = i;

                while (i < 8 && groups[i] == 0)
                {
                    i++;
                }
                /* Two or more: a single zero group is never compressed. */
                if (i - start >= 2 && i - start > best_length)
                {
                    best_start = start;
                    best_length = i - start;
                }
            }
        }

        for (i = 0; i < 8; i++)
        {
            if (best_start >= 0 && i >= best_start && i < best_start + best_length)
            {
                if (i == best_start && used + 2 < size)
                {
                    buf[used++] = ':';
                    buf[used++] = ':';
                }
                continue;
            }

            /* A separator, unless this is the first thing or a colon precedes it. */
            if (used > 0 && buf[used - 1] != ':' && used + 1 < size)
            {
                buf[used++] = ':';
            }
            if (used + 5 < size)
            {
                used += (size_t)snprintf(buf + used, size - used, "%x", groups[i]);
            }
        }

        /* An all-zero address compresses to "::", which the loop already emitted. */
        if (used < size)
        {
            buf[used] = '\0';
        }
    }
}

ql_ret_t ql_net_addr_format(const ql_net_addr_t* addr, char* buf, size_t size)
{
    int written;

    if (addr == NULL || buf == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_format: addr or buf is NULL");
        return QL_ERR_INVALID_ARG;
    }

    if (addr->family == QL_NET_AF_INET6)
    {
        if (size < QL_NET_ADDR_STR_MAX)
        {
            ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_net_addr_format: buffer is too small for IPv6");
            return QL_ERR_OUT_OF_RANGE;
        }
        ql_net_format_ipv6(addr->bytes, buf, size);
        return QL_OK;
    }

    if (addr->family != QL_NET_AF_INET)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_format: not an address");
        return QL_ERR_INVALID_ARG;
    }

    if (size < QL_NET_IPV4_STR_SIZE)
    {
        ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_net_addr_format: buffer is too small");
        return QL_ERR_OUT_OF_RANGE;
    }

    written = snprintf(buf, size, "%u.%u.%u.%u", (unsigned)addr->bytes[0], (unsigned)addr->bytes[1],
                       (unsigned)addr->bytes[2], (unsigned)addr->bytes[3]);
    if (written < 0 || (size_t)written >= size)
    {
        ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_net_addr_format: buffer is too small");
        return QL_ERR_OUT_OF_RANGE;
    }

    return QL_OK;
}

bool ql_net_addr_equal(const ql_net_addr_t* a, const ql_net_addr_t* b)
{
    size_t length;

    if (a == NULL || b == NULL || a->family != b->family)
    {
        return false;
    }

    /* Compare only the bytes the family actually uses, so padding is ignored. */
    length = a->family == QL_NET_AF_INET ? 4U : sizeof a->bytes;
    return memcmp(a->bytes, b->bytes, length) == 0;
}

/* ---------------------------------------------------------------------------
 * Sockets
 * ------------------------------------------------------------------------ */

/** @brief The platform's "last socket error" value. */
static int ql_net_last_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

/**
 * @brief Map a platform socket error onto ql_ret_t.
 *
 * Connection refused, reset and unreachable all collapse to QL_ERR_IO: from a
 * caller's point of view they are the same event with different errands, and
 * the error handler's message carries the detail.
 */
static ql_ret_t ql_net_error_to_ret(int error)
{
#ifdef _WIN32
    switch (error)
    {
    case WSAEWOULDBLOCK:
        return QL_ERR_AGAIN;
    case WSAETIMEDOUT:
        return QL_ERR_TIMEOUT;
    case WSAECONNREFUSED:
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENOTCONN:
    case WSAEHOSTUNREACH:
    case WSAENETUNREACH:
        return QL_ERR_IO;
    default:
        return QL_ERR_SYS;
    }
#else
    switch (error)
    {
    case EAGAIN: /* also EWOULDBLOCK, and what an expired SO_RCVTIMEO gives */
    case EINTR:
        return QL_ERR_AGAIN;
    case ETIMEDOUT:
        return QL_ERR_TIMEOUT;
    case ECONNREFUSED:
    case ECONNRESET:
    case ECONNABORTED:
    case ENOTCONN:
    case EHOSTUNREACH:
    case ENETUNREACH:
    case EPIPE:
        return QL_ERR_IO;
    default:
        return QL_ERR_SYS;
    }
#endif
}

ql_ret_t ql_net_init(void)
{
    if (g_net_started)
    {
        return QL_OK;
    }

#ifdef _WIN32
    {
        WSADATA data;

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            ql_err_raise(QL_ERR_SYS, "ql_net_init: WSAStartup failed");
            return QL_ERR_SYS;
        }
    }
#endif

    g_net_started = true;
    return QL_OK;
}

/** @brief Map a getaddrinfo() result code onto ql_ret_t. */
static ql_ret_t ql_net_resolve_error(int code)
{
    /* The EAI_* values differ between platforms, so they are compared by name
     * rather than by number. */
    if (code == EAI_NONAME || code == EAI_FAIL)
    {
        return QL_ERR_NOT_FOUND;
    }
    if (code == EAI_AGAIN)
    {
        return QL_ERR_AGAIN;
    }
    if (code == EAI_MEMORY)
    {
        return QL_ERR_NOMEM;
    }
    return QL_ERR_SYS;
}

ql_ret_t ql_net_addr_resolve(const char* host, ql_net_addr_t* out)
{
    struct addrinfo hints;
    struct addrinfo* results = NULL;
    ql_ret_t rc;
    int status;

    if (host == NULL || out == NULL || host[0] == '\0')
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_resolve: host or out is NULL, or host is empty");
        return QL_ERR_INVALID_ARG;
    }

    rc = ql_net_init();
    if (rc != QL_OK)
    {
        return rc;
    }

    memset(&hints, 0, sizeof hints);
    /* IPv4 only: this module has no IPv6 sockets, and returning an address it
     * cannot connect to would only move the failure somewhere less clear. */
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(host, NULL, &hints, &results);
    if (status != 0)
    {
        ql_ret_t code = ql_net_resolve_error(status);

        if (code == QL_ERR_NOT_FOUND)
        {
            /* A name that does not resolve is an answer, not a fault. */
            freeaddrinfo(results);
            return code;
        }
        ql_err_raise(code, "ql_net_addr_resolve: getaddrinfo failed");
        freeaddrinfo(results);
        return code;
    }

    if (results == NULL || results->ai_addr == NULL)
    {
        freeaddrinfo(results);
        return QL_ERR_NOT_FOUND;
    }

    memset(out, 0, sizeof *out);
    out->family = QL_NET_AF_INET;
    memcpy(out->bytes, &((const struct sockaddr_in*)(void*)results->ai_addr)->sin_addr, 4);

    freeaddrinfo(results);
    return QL_OK;
}

void ql_net_shutdown(void)
{
    if (!g_net_started)
    {
        return;
    }

#ifdef _WIN32
    (void)WSACleanup();
#endif

    g_net_started = false;
}

/** @brief Start the platform layer if needed, and check the address family. */
static ql_ret_t ql_net_prepare(const ql_net_addr_t* addr)
{
    ql_ret_t rc = ql_net_init();

    if (rc != QL_OK)
    {
        return rc;
    }
    if (addr->family == QL_NET_AF_INET6)
    {
        ql_err_raise(QL_ERR_NOT_SUPPORTED, "ql_net: IPv6 sockets are not supported yet");
        return QL_ERR_NOT_SUPPORTED;
    }
    if (addr->family != QL_NET_AF_INET)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net: not an IPv4 address");
        return QL_ERR_INVALID_ARG;
    }
    return QL_OK;
}

/** @brief Fill a sockaddr_in from an address and a host order port. */
static void ql_net_to_sockaddr(const ql_net_addr_t* addr, uint16_t port, struct sockaddr_in* out)
{
    memset(out, 0, sizeof *out);
    out->sin_family = AF_INET;
    out->sin_port = htons(port);
    memcpy(&out->sin_addr, addr->bytes, 4);
}

/** @brief Allocate the wrapper for an already-open platform handle. */
static ql_ret_t ql_net_socket_wrap(const ql_allocator_t* alloc, ql_net_handle_t handle, ql_net_socket_t** out)
{
    ql_net_socket_t* sock = ql_alloc(alloc, sizeof *sock);

    if (sock == NULL)
    {
        return QL_ERR_NOMEM;
    }

    sock->alloc = alloc != NULL ? *alloc : ql_default_allocator();
    sock->handle = handle;
    sock->timeout_ms = 0;

    *out = sock;
    return QL_OK;
}

/** @brief Close a platform handle, ignoring the result. */
static void ql_net_handle_close(ql_net_handle_t handle)
{
#ifdef _WIN32
    (void)closesocket(handle);
#else
    (void)close(handle);
#endif
}

/** @brief Set SO_REUSEADDR so a restarted server is not blocked by TIME_WAIT. */
static void ql_net_set_reuseaddr(ql_net_handle_t handle)
{
    int one = 1;

#ifdef _WIN32
    (void)setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, QL_NET_SOCK_OPT_CAST & one, (int)sizeof one);
#else
    (void)setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof one);
#endif
}

ql_ret_t ql_net_tcp_listen(const ql_allocator_t* alloc, const ql_net_addr_t* addr, uint16_t port, int backlog,
                           ql_net_socket_t** out)
{
    struct sockaddr_in local;
    ql_net_handle_t handle;
    ql_ret_t rc;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_tcp_listen: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    /* Clear the caller's handle before any check can fail, as documented. */
    *out = NULL;

    if (addr == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_tcp_listen: addr is NULL");
        return QL_ERR_INVALID_ARG;
    }

    rc = ql_net_prepare(addr);
    if (rc != QL_OK)
    {
        return rc;
    }

    handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == QL_NET_INVALID_HANDLE)
    {
        ql_err_raise(ql_net_error_to_ret(ql_net_last_error()), "ql_net_tcp_listen: socket() failed");
        return ql_net_error_to_ret(ql_net_last_error());
    }

    ql_net_set_reuseaddr(handle);
    ql_net_to_sockaddr(addr, port, &local);

    if (bind(handle, (const struct sockaddr*)&local, (ql_net_socklen_t)sizeof local) != 0)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        ql_net_handle_close(handle);
        ql_err_raise(code, "ql_net_tcp_listen: bind() failed");
        return code;
    }

    if (listen(handle, backlog < 1 ? 1 : backlog) != 0)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        ql_net_handle_close(handle);
        ql_err_raise(code, "ql_net_tcp_listen: listen() failed");
        return code;
    }

    rc = ql_net_socket_wrap(alloc, handle, out);
    if (rc != QL_OK)
    {
        ql_net_handle_close(handle);
        return rc;
    }
    return QL_OK;
}

ql_ret_t ql_net_tcp_connect(const ql_allocator_t* alloc, const ql_net_addr_t* addr, uint16_t port,
                            ql_net_socket_t** out)
{
    struct sockaddr_in remote;
    ql_net_handle_t handle;
    ql_ret_t rc;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_tcp_connect: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    /* Clear the caller's handle before any check can fail, as documented. */
    *out = NULL;

    if (addr == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_tcp_connect: addr is NULL");
        return QL_ERR_INVALID_ARG;
    }

    rc = ql_net_prepare(addr);
    if (rc != QL_OK)
    {
        return rc;
    }

    handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (handle == QL_NET_INVALID_HANDLE)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        ql_err_raise(code, "ql_net_tcp_connect: socket() failed");
        return code;
    }

    ql_net_to_sockaddr(addr, port, &remote);

    if (connect(handle, (const struct sockaddr*)&remote, (ql_net_socklen_t)sizeof remote) != 0)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        ql_net_handle_close(handle);
        ql_err_raise(code, "ql_net_tcp_connect: connect() failed");
        return code;
    }

    rc = ql_net_socket_wrap(alloc, handle, out);
    if (rc != QL_OK)
    {
        ql_net_handle_close(handle);
        return rc;
    }
    return QL_OK;
}

/**
 * @brief Wait until a socket is ready, or the socket's timeout expires.
 *
 * The wait is done here rather than left to `SO_RCVTIMEO` because the platforms
 * disagree about it: an expired receive timeout is `EAGAIN` on POSIX and
 * `WSAETIMEDOUT` on Windows, and on Windows it does not reliably interrupt
 * `accept()` at all -- which turns a bounded wait into a hung test run. `select`
 * means one timeout, one error code, on every platform.
 *
 * @return QL_OK when ready, QL_ERR_TIMEOUT when the wait expired, or whatever
 *         the platform reported.
 */
static ql_ret_t ql_net_wait(const ql_net_socket_t* sock, bool for_write)
{
    fd_set set;
    struct timeval timeout;
    struct timeval* limit = NULL;
    int ready;

    if (sock->timeout_ms <= 0)
    {
        /* Waiting forever: let the blocking call do the waiting. */
        return QL_OK;
    }

    FD_ZERO(&set);
    FD_SET(sock->handle, &set);

    timeout.tv_sec = (time_t)(sock->timeout_ms / 1000);
    timeout.tv_usec = (long)((sock->timeout_ms % 1000) * 1000);
    limit = &timeout;

    /* The first argument is ignored on Windows and is +1 on POSIX. */
    ready = select((int)sock->handle + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, limit);
    if (ready == 0)
    {
        return QL_ERR_TIMEOUT;
    }
    if (ready < 0)
    {
        return ql_net_error_to_ret(ql_net_last_error());
    }
    return QL_OK;
}

ql_ret_t ql_net_accept(const ql_net_socket_t* listener, ql_net_socket_t** out)
{
    ql_net_handle_t handle;
    ql_ret_t rc;

    if (listener == NULL || out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_accept: listener or out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    rc = ql_net_wait(listener, false);
    if (rc != QL_OK)
    {
        /* A timeout is a normal answer, not a fault worth reporting. */
        if (rc != QL_ERR_TIMEOUT)
        {
            ql_err_raise(rc, "ql_net_accept: waiting for a connection failed");
        }
        return rc;
    }

    handle = accept(listener->handle, NULL, NULL);
    if (handle == QL_NET_INVALID_HANDLE)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        ql_err_raise(code, "ql_net_accept: accept() failed");
        return code;
    }

    /* The accepted socket inherits the listener's allocator. */
    rc = ql_net_socket_wrap(&listener->alloc, handle, out);
    if (rc != QL_OK)
    {
        ql_net_handle_close(handle);
        return rc;
    }
    return QL_OK;
}

ql_ret_t ql_net_send(ql_net_socket_t* sock, const void* data, size_t len, size_t* out_sent)
{
    ql_net_io_result_t written;
    size_t chunk = len;

    if (sock == NULL || (data == NULL && len != 0) || out_sent == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_send: sock, data or out_sent is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out_sent = 0;

    if (len == 0)
    {
        return QL_OK;
    }

    /* Both platforms take and return int here. */
    if (chunk > (size_t)INT_MAX)
    {
        chunk = (size_t)INT_MAX;
    }

    {
        ql_ret_t rc = ql_net_wait(sock, true);

        if (rc != QL_OK)
        {
            return rc;
        }
    }

    written = send(sock->handle, data, QL_NET_IO_LEN(chunk), 0);
    if (written < 0)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        if (code != QL_ERR_AGAIN)
        {
            ql_err_raise(code, "ql_net_send: send() failed");
        }
        return code;
    }

    *out_sent = (size_t)written;
    return QL_OK;
}

ql_ret_t ql_net_recv(ql_net_socket_t* sock, void* buf, size_t cap, size_t* out_received)
{
    ql_net_io_result_t got;
    size_t chunk = cap;

    if (sock == NULL || (buf == NULL && cap != 0) || out_received == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_recv: sock, buf or out_received is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out_received = 0;

    if (cap == 0)
    {
        return QL_OK;
    }

    if (chunk > (size_t)INT_MAX)
    {
        chunk = (size_t)INT_MAX;
    }

    {
        ql_ret_t rc = ql_net_wait(sock, false);

        if (rc != QL_OK)
        {
            return rc;
        }
    }

    got = recv(sock->handle, buf, QL_NET_IO_LEN(chunk), 0);
    if (got < 0)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        if (code != QL_ERR_AGAIN)
        {
            ql_err_raise(code, "ql_net_recv: recv() failed");
        }
        return code;
    }

    /* A zero byte read is the peer's orderly shutdown, not a failure. */
    *out_received = (size_t)got;
    return QL_OK;
}

ql_ret_t ql_net_set_timeout(ql_net_socket_t* sock, int millis)
{
    if (sock == NULL || millis < 0)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_set_timeout: sock is NULL or millis is negative");
        return QL_ERR_INVALID_ARG;
    }

    /* Recorded here, and enforced by ql_net_wait(); the setsockopt below is a
     * backstop for calls that qlib does not wrap. */
    sock->timeout_ms = millis;

#ifdef _WIN32
    {
        DWORD value = (DWORD)millis;

        if (setsockopt(sock->handle, SOL_SOCKET, SO_RCVTIMEO, QL_NET_SOCK_OPT_CAST & value, (int)sizeof value) != 0 ||
            setsockopt(sock->handle, SOL_SOCKET, SO_SNDTIMEO, QL_NET_SOCK_OPT_CAST & value, (int)sizeof value) != 0)
        {
            ql_err_raise(QL_ERR_SYS, "ql_net_set_timeout: setsockopt failed");
            return QL_ERR_SYS;
        }
    }
#else
    {
        struct timeval value;

        /* Both fields are aliases of a signed long on every platform qlib
         * supports, but they are not spelled that way, so cast explicitly. */
        value.tv_sec = (time_t)(millis / 1000);
        value.tv_usec = (long)((millis % 1000) * 1000);

        if (setsockopt(sock->handle, SOL_SOCKET, SO_RCVTIMEO, &value, (socklen_t)sizeof value) != 0 ||
            setsockopt(sock->handle, SOL_SOCKET, SO_SNDTIMEO, &value, (socklen_t)sizeof value) != 0)
        {
            ql_err_raise(QL_ERR_SYS, "ql_net_set_timeout: setsockopt failed");
            return QL_ERR_SYS;
        }
    }
#endif

    return QL_OK;
}

ql_ret_t ql_net_local_port(const ql_net_socket_t* sock, uint16_t* out_port)
{
    struct sockaddr_in local;
    ql_net_socklen_t length = (ql_net_socklen_t)sizeof local;

    if (sock == NULL || out_port == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_local_port: sock or out_port is NULL");
        return QL_ERR_INVALID_ARG;
    }

    memset(&local, 0, sizeof local);
    if (getsockname(sock->handle, (struct sockaddr*)&local, &length) != 0)
    {
        ql_err_raise(QL_ERR_SYS, "ql_net_local_port: getsockname() failed");
        return QL_ERR_SYS;
    }

    *out_port = ntohs(local.sin_port);
    return QL_OK;
}

void ql_net_socket_close(ql_net_socket_t* sock)
{
    ql_allocator_t alloc;

    if (sock == NULL)
    {
        return;
    }

    /* Copy the allocator out before the control block itself goes away. */
    alloc = sock->alloc;
    ql_net_handle_close(sock->handle);
    ql_dealloc(&alloc, sock);
}
