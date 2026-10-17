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
};

/** @brief Whether the platform socket layer has been started. */
static bool g_net_started = false;

/* "255.255.255.255" plus the terminator. */
#define QL_NET_IPV4_STR_SIZE 16

static bool ql_net_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

ql_ret_t ql_net_addr_parse(const char* text, ql_net_addr_t* out)
{
    ql_net_addr_t addr;
    const char* p;
    size_t index;

    if (text == NULL || out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: text or out is NULL");
        return QL_ERR_INVALID_ARG;
    }

    /*
     * A colon means the caller wrote IPv6. Saying "not supported" is more
     * useful than the syntax error the octet parser would otherwise report.
     */
    if (strchr(text, ':') != NULL)
    {
        ql_err_raise(QL_ERR_NOT_SUPPORTED, "ql_net_addr_parse: IPv6 is not supported yet");
        return QL_ERR_NOT_SUPPORTED;
    }

    memset(&addr, 0, sizeof addr);
    p = text;

    for (index = 0; index < 4; index++)
    {
        unsigned value = 0;
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

        while (ql_net_is_digit(*p))
        {
            /*
             * Reject "010" rather than pick a base for it. inet_pton() on glibc
             * also rejects it, and silently choosing octal here would be the
             * kind of surprise that turns into a security bug elsewhere.
             */
            if (digits == 0 && *p == '0' && ql_net_is_digit(p[1]))
            {
                ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: octet has a leading zero");
                return QL_ERR_INVALID_ARG;
            }

            value = (value * 10U) + (unsigned)(*p - '0');
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

        addr.bytes[index] = (unsigned char)value;
    }

    /* Exactly four octets, and nothing left over. */
    if (*p != '\0')
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: trailing characters after the address");
        return QL_ERR_INVALID_ARG;
    }

    addr.family = QL_NET_AF_INET;
    *out = addr;
    return QL_OK;
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
        ql_err_raise(QL_ERR_NOT_SUPPORTED, "ql_net_addr_format: IPv6 is not supported yet");
        return QL_ERR_NOT_SUPPORTED;
    }
    if (addr->family != QL_NET_AF_INET)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_format: not an IPv4 address");
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

    handle = accept(listener->handle, NULL, NULL);
    if (handle == QL_NET_INVALID_HANDLE)
    {
        ql_ret_t code = ql_net_error_to_ret(ql_net_last_error());

        /* Nothing pending is not worth reporting; a timeout is a normal answer. */
        if (code != QL_ERR_AGAIN)
        {
            ql_err_raise(code, "ql_net_accept: accept() failed");
        }
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
