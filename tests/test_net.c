#include "test_suites.h"

#include "qlib/ql_net.h"

#include <stdio.h>
#include <string.h>

#include "test_alloc.h"

/* ---------------------------------------------------------------------------
 * A local error-handler probe. The handler mechanism itself is covered by
 * test_mem; here it only has to prove that ql_net raises as well as returns.
 * ------------------------------------------------------------------------ */

typedef struct
{
    int calls;
    ql_ret_t code;
    const char* message;
} net_probe;

static net_probe g_probe;

static void net_probe_record(ql_ret_t code, const char* message, void* userdata)
{
    net_probe* probe = userdata;

    probe->calls++;
    probe->code = code;
    probe->message = message;
}

static void probe_begin(void)
{
    g_probe.calls = 0;
    g_probe.code = QL_OK;
    g_probe.message = NULL;
    ql_set_error_handler(net_probe_record, &g_probe);
}

static void probe_expect(int calls, ql_ret_t code)
{
    if (g_probe.calls != calls)
    {
        fprintf(stderr, "  handler called %d time(s), expected %d\n", g_probe.calls, calls);
    }
    MU_CHECK(g_probe.calls == calls);
    MU_CHECK(g_probe.code == code);
    MU_CHECK(g_probe.message != NULL);
}

/*
 * Small helpers that report the offending input before the assertion fires.
 * minunit prints only file and line, and a table of two dozen malformed inputs
 * is useless to debug without knowing which one failed.
 */

static void expect_parse(const char* text, ql_ret_t expected)
{
    ql_net_addr_t addr;
    ql_ret_t rc = ql_net_addr_parse(text, &addr);

    if (rc != expected)
    {
        fprintf(stderr, "  parse(\"%s\") -> %s, expected %s\n", text, ql_strerror(rc), ql_strerror(expected));
    }
    MU_CHECK(rc == expected);
}

static void expect_round_trip(const char* text)
{
    ql_net_addr_t addr;
    char buf[QL_NET_ADDR_STR_MAX];

    MU_CHECK(ql_net_addr_parse(text, &addr) == QL_OK);
    memset(buf, 0, sizeof buf);
    MU_CHECK(ql_net_addr_format(&addr, buf, sizeof buf) == QL_OK);
    MU_CHECK(strcmp(buf, text) == 0);
}

MU_TEST(test_parse_accepts_valid_addresses)
{
    ql_net_addr_t addr;

    MU_ASSERT(ql_net_addr_parse("0.0.0.0", &addr) == QL_OK);
    MU_CHECK(addr.family == QL_NET_AF_INET);
    MU_CHECK(addr.bytes[0] == 0 && addr.bytes[1] == 0 && addr.bytes[2] == 0 && addr.bytes[3] == 0);

    MU_ASSERT(ql_net_addr_parse("255.255.255.255", &addr) == QL_OK);
    MU_CHECK(addr.bytes[0] == 255 && addr.bytes[1] == 255 && addr.bytes[2] == 255 && addr.bytes[3] == 255);

    MU_ASSERT(ql_net_addr_parse("127.0.0.1", &addr) == QL_OK);
    MU_CHECK(addr.bytes[0] == 127 && addr.bytes[3] == 1);

    MU_ASSERT(ql_net_addr_parse("10.20.30.40", &addr) == QL_OK);
    MU_CHECK(addr.bytes[0] == 10 && addr.bytes[1] == 20 && addr.bytes[2] == 30 && addr.bytes[3] == 40);
}

MU_TEST(test_parse_zeroes_the_whole_struct)
{
    ql_net_addr_t addr;
    size_t i;

    memset(&addr, 0xEE, sizeof addr);
    MU_ASSERT(ql_net_addr_parse("1.2.3.4", &addr) == QL_OK);

    /* Unused address bytes and reserved must be zero, not leftovers. */
    for (i = 4; i < sizeof addr.bytes; i++)
    {
        MU_CHECK(addr.bytes[i] == 0);
    }
    for (i = 0; i < sizeof addr.reserved / sizeof addr.reserved[0]; i++)
    {
        MU_CHECK(addr.reserved[i] == NULL);
    }
}

MU_TEST(test_round_trips)
{
    expect_round_trip("0.0.0.0");
    expect_round_trip("255.255.255.255");
    expect_round_trip("127.0.0.1");
    expect_round_trip("192.168.1.1");
    expect_round_trip("8.8.8.8");
    expect_round_trip("1.22.233.4");
}

MU_TEST(test_parse_rejects_malformed_text)
{
    static const char* const cases[] = {
        "",          /* empty */
        ".",         /* nothing but a separator */
        "1",         /* too few octets */
        "1.2",       /* too few octets */
        "1.2.3",     /* too few octets */
        "1.2.3.4.5", /* too many octets */
        "1.2.3.4.",  /* trailing separator */
        ".1.2.3.4",  /* leading separator */
        "1..3.4",    /* empty octet in the middle */
        "1.2.3.",    /* empty final octet */
        "a.b.c.d",   /* not digits */
        "1.2.3.d",   /* trailing letter */
        "1.2.3.4x",  /* trailing junk */
        "1.2.3.4 ",  /* trailing space */
        " 1.2.3.4",  /* leading space */
        "1.2.3.4\n", /* trailing newline */
        "1.2.3.-1",  /* sign */
        "1.2.3.+1",  /* sign */
        "1.2.3.4.5.6",
        "+1.2.3.4",
        "1,2,3,4", /* wrong separator */
        "1.2.3.4/24",
    };
    size_t i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        expect_parse(cases[i], QL_ERR_INVALID_ARG);
    }
}

MU_TEST(test_parse_rejects_ambiguous_leading_zeros)
{
    /* "010" could be 10 or 8; refuse to guess rather than pick one. */
    expect_parse("010.0.0.1", QL_ERR_INVALID_ARG);
    expect_parse("01.2.3.4", QL_ERR_INVALID_ARG);
    expect_parse("1.2.3.04", QL_ERR_INVALID_ARG);
    expect_parse("00.0.0.0", QL_ERR_INVALID_ARG);

    /* A lone zero is unambiguous and must stay legal. */
    expect_parse("0.0.0.0", QL_OK);
    expect_parse("0.0.0.10", QL_OK);
    expect_parse("1.0.0.0", QL_OK);
}

MU_TEST(test_parse_rejects_out_of_range_octets)
{
    expect_parse("1.2.3.256", QL_ERR_OUT_OF_RANGE);
    expect_parse("256.0.0.1", QL_ERR_OUT_OF_RANGE);
    expect_parse("1.256.3.4", QL_ERR_OUT_OF_RANGE);
    expect_parse("999.1.1.1", QL_ERR_OUT_OF_RANGE);
    /* Far beyond a single octet, so the accumulator itself must not wrap. */
    expect_parse("1.2.3.4294967296", QL_ERR_OUT_OF_RANGE);
    expect_parse("1.2.3.18446744073709551616", QL_ERR_OUT_OF_RANGE);
}

MU_TEST(test_parse_reports_ipv6_as_unsupported)
{
    expect_parse("::1", QL_ERR_NOT_SUPPORTED);
    expect_parse("::", QL_ERR_NOT_SUPPORTED);
    expect_parse("2001:db8::1", QL_ERR_NOT_SUPPORTED);
    expect_parse("fe80::1%eth0", QL_ERR_NOT_SUPPORTED);
    expect_parse(":", QL_ERR_NOT_SUPPORTED);
}

MU_TEST(test_parse_rejects_null_arguments)
{
    ql_net_addr_t addr;

    MU_CHECK(ql_net_addr_parse(NULL, &addr) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_addr_parse("1.2.3.4", NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_addr_parse(NULL, NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_format_writes_the_address)
{
    ql_net_addr_t addr;
    char buf[QL_NET_ADDR_STR_MAX];

    MU_ASSERT(ql_net_addr_parse("203.0.113.7", &addr) == QL_OK);
    memset(buf, 0x7F, sizeof buf);
    MU_ASSERT(ql_net_addr_format(&addr, buf, sizeof buf) == QL_OK);
    MU_CHECK(strcmp(buf, "203.0.113.7") == 0);

    /* The shortest legal buffer is exactly the longest IPv4 text plus NUL. */
    memset(buf, 0x7F, sizeof buf);
    MU_ASSERT(ql_net_addr_parse("255.255.255.255", &addr) == QL_OK);
    MU_ASSERT(ql_net_addr_format(&addr, buf, 16) == QL_OK);
    MU_CHECK(strcmp(buf, "255.255.255.255") == 0);
}

MU_TEST(test_format_rejects_short_buffers_without_writing)
{
    ql_net_addr_t addr;
    char buf[32];
    size_t i;
    size_t size;

    MU_ASSERT(ql_net_addr_parse("1.2.3.4", &addr) == QL_OK);

    for (size = 0; size < 16; size++)
    {
        memset(buf, 0x7F, sizeof buf);
        MU_CHECK(ql_net_addr_format(&addr, buf, size) == QL_ERR_OUT_OF_RANGE);

        /* Documented: a failed format leaves the buffer untouched. */
        for (i = 0; i < sizeof buf; i++)
        {
            MU_CHECK(buf[i] == 0x7F);
        }
    }
}

MU_TEST(test_format_rejects_bad_arguments_and_families)
{
    ql_net_addr_t addr;
    ql_net_addr_t unspec;
    char buf[QL_NET_ADDR_STR_MAX];

    memset(&unspec, 0, sizeof unspec);
    MU_CHECK(unspec.family == QL_NET_AF_UNSPEC);

    MU_CHECK(ql_net_addr_format(NULL, buf, sizeof buf) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_addr_format(&unspec, NULL, sizeof buf) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_addr_format(&unspec, buf, sizeof buf) == QL_ERR_INVALID_ARG);

    MU_ASSERT(ql_net_addr_parse("1.2.3.4", &addr) == QL_OK);
    addr.family = QL_NET_AF_INET6;
    MU_CHECK(ql_net_addr_format(&addr, buf, sizeof buf) == QL_ERR_NOT_SUPPORTED);

    /*
     * A family outside the enum is not an address either. Casting out of range
     * is the point of the test: the value can arrive from a corrupted struct.
     */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- intentional
    addr.family = (ql_net_family_t)99;
    MU_CHECK(ql_net_addr_format(&addr, buf, sizeof buf) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_equality)
{
    ql_net_addr_t a;
    ql_net_addr_t b;
    ql_net_addr_t c;

    MU_ASSERT(ql_net_addr_parse("192.168.1.1", &a) == QL_OK);
    MU_ASSERT(ql_net_addr_parse("192.168.1.1", &b) == QL_OK);
    MU_ASSERT(ql_net_addr_parse("192.168.1.2", &c) == QL_OK);

    MU_CHECK(ql_net_addr_equal(&a, &a));
    MU_CHECK(ql_net_addr_equal(&a, &b));
    MU_CHECK(!ql_net_addr_equal(&a, &c));

    /* A NULL is not equal to anything, including another NULL. */
    MU_CHECK(!ql_net_addr_equal(&a, NULL));
    MU_CHECK(!ql_net_addr_equal(NULL, &a));
    MU_CHECK(!ql_net_addr_equal(NULL, NULL));

    /* Family is part of identity even when the bytes agree. */
    b.family = QL_NET_AF_INET6;
    MU_CHECK(!ql_net_addr_equal(&a, &b));

    /* Bytes past the family's length are padding and must not be compared. */
    MU_ASSERT(ql_net_addr_parse("10.0.0.1", &b) == QL_OK);
    b.bytes[8] = 0xFF;
    MU_CHECK(ql_net_addr_equal(&a, &a));
    MU_CHECK(ql_net_addr_equal(&b, &b));
}

MU_TEST(test_failures_are_raised_not_just_returned)
{
    ql_net_addr_t addr;
    char buf[QL_NET_ADDR_STR_MAX];

    /* One failing call, one report: the return value and the handler agree. */
    probe_begin();
    MU_CHECK(ql_net_addr_parse("1.2.3.999", &addr) == QL_ERR_OUT_OF_RANGE);
    probe_expect(1, QL_ERR_OUT_OF_RANGE);

    MU_CHECK(ql_net_addr_parse("1.2.3", &addr) == QL_ERR_INVALID_ARG);
    probe_expect(2, QL_ERR_INVALID_ARG);

    MU_CHECK(ql_net_addr_parse("::1", &addr) == QL_ERR_NOT_SUPPORTED);
    probe_expect(3, QL_ERR_NOT_SUPPORTED);

    /* A successful call must stay silent. */
    MU_ASSERT(ql_net_addr_parse("1.2.3.4", &addr) == QL_OK);
    probe_expect(3, QL_ERR_NOT_SUPPORTED);

    MU_CHECK(ql_net_addr_format(&addr, buf, 4) == QL_ERR_OUT_OF_RANGE);
    probe_expect(4, QL_ERR_OUT_OF_RANGE);

    ql_set_error_handler(NULL, NULL);
}

/* ---------------------------------------------------------------------------
 * Sockets
 *
 * These run on loopback and need no peer thread: the kernel completes a
 * connection into the listener's backlog before accept() is called, so
 * connect-then-accept is deterministic in one thread. Every socket gets a
 * timeout so that a bug fails the test instead of hanging CI.
 * ------------------------------------------------------------------------ */

#define NET_TIMEOUT_MS 2000

/*
 * Neither helper uses MU_ASSERT: it expands to a bare `return`, which a
 * function returning a value cannot use. Callers assert on the result instead,
 * and minunit reports against the caller's line, which is the line that needed
 * the socket.
 */

/** @brief 127.0.0.1, or a zeroed address if parsing somehow fails. */
static ql_net_addr_t loopback_address(void)
{
    ql_net_addr_t addr;

    memset(&addr, 0, sizeof addr);
    (void)ql_net_addr_parse("127.0.0.1", &addr);
    return addr;
}

/** @brief A listening socket on an ephemeral loopback port, or NULL. */
static ql_net_socket_t* make_listener(uint16_t* out_port)
{
    ql_net_addr_t addr = loopback_address();
    ql_net_socket_t* listener = NULL;

    if (ql_net_tcp_listen(NULL, &addr, 0, 4, &listener) != QL_OK)
    {
        return NULL;
    }
    if (ql_net_local_port(listener, out_port) != QL_OK || *out_port == 0 ||
        ql_net_set_timeout(listener, NET_TIMEOUT_MS) != QL_OK)
    {
        ql_net_socket_close(listener);
        return NULL;
    }
    return listener;
}

MU_TEST(test_resolve_numeric_and_local_names)
{
    ql_net_addr_t addr;
    ql_net_addr_t expected;
    unsigned char loopback[4] = {127, 0, 0, 1};

    /* A numeric address needs no lookup at all. */
    MU_ASSERT(ql_net_addr_resolve("127.0.0.1", &addr) == QL_OK);
    MU_CHECK(addr.family == QL_NET_AF_INET);
    MU_CHECK(memcmp(addr.bytes, loopback, 4) == 0);

    /* "localhost" comes from the hosts file, so this needs no network either. */
    MU_ASSERT(ql_net_addr_resolve("localhost", &addr) == QL_OK);
    MU_CHECK(addr.family == QL_NET_AF_INET);
    MU_ASSERT(ql_net_addr_parse("127.0.0.1", &expected) == QL_OK);
    MU_CHECK(ql_net_addr_equal(&addr, &expected));

    MU_CHECK(ql_net_addr_resolve(NULL, &addr) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_addr_resolve("", &addr) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_addr_resolve("localhost", NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_connect_by_resolved_name)
{
    ql_net_socket_t* listener;
    ql_net_socket_t* client = NULL;
    ql_net_socket_t* server = NULL;
    ql_net_addr_t resolved;
    uint16_t port = 0;

    listener = make_listener(&port);
    MU_ASSERT(listener != NULL);

    /* The point of resolving: the result has to be usable as an address. */
    MU_ASSERT(ql_net_addr_resolve("localhost", &resolved) == QL_OK);
    MU_ASSERT(ql_net_tcp_connect(NULL, &resolved, port, &client) == QL_OK);
    MU_ASSERT(ql_net_accept(listener, &server) == QL_OK);

    ql_net_socket_close(server);
    ql_net_socket_close(client);
    ql_net_socket_close(listener);
}

MU_TEST(test_listen_picks_a_port)
{
    ql_net_socket_t* listener;
    uint16_t port = 0;

    listener = make_listener(&port);
    MU_ASSERT(listener != NULL);
    MU_CHECK(port != 0);

    ql_net_socket_close(listener);
    ql_net_socket_close(NULL);
}

MU_TEST(test_loopback_round_trip)
{
    ql_net_addr_t addr = loopback_address();
    ql_net_socket_t* listener;
    ql_net_socket_t* client = NULL;
    ql_net_socket_t* server = NULL;
    uint16_t port = 0;
    size_t moved = 0;
    char buffer[16];

    listener = make_listener(&port);
    MU_ASSERT(listener != NULL);

    /* Connect first: the kernel completes the handshake into the backlog, so
     * the following accept() has something pending and cannot block. */
    MU_ASSERT(ql_net_tcp_connect(NULL, &addr, port, &client) == QL_OK);
    MU_ASSERT(ql_net_set_timeout(client, NET_TIMEOUT_MS) == QL_OK);

    MU_ASSERT(ql_net_accept(listener, &server) == QL_OK);
    MU_ASSERT(ql_net_set_timeout(server, NET_TIMEOUT_MS) == QL_OK);

    /* Client sends first, so the server's recv has data waiting. */
    MU_ASSERT(ql_net_send(client, "ping", 4, &moved) == QL_OK);
    MU_CHECK(moved == 4);

    memset(buffer, 0, sizeof buffer);
    MU_ASSERT(ql_net_recv(server, buffer, sizeof buffer, &moved) == QL_OK);
    MU_CHECK(moved == 4);
    MU_CHECK(memcmp(buffer, "ping", 4) == 0);

    /* And back the other way. */
    MU_ASSERT(ql_net_send(server, "pong", 4, &moved) == QL_OK);
    MU_CHECK(moved == 4);

    memset(buffer, 0, sizeof buffer);
    MU_ASSERT(ql_net_recv(client, buffer, sizeof buffer, &moved) == QL_OK);
    MU_CHECK(moved == 4);
    MU_CHECK(memcmp(buffer, "pong", 4) == 0);

    ql_net_socket_close(server);
    ql_net_socket_close(client);
    ql_net_socket_close(listener);
}

MU_TEST(test_recv_reports_a_closed_peer_as_zero_bytes)
{
    ql_net_addr_t addr = loopback_address();
    ql_net_socket_t* listener;
    ql_net_socket_t* client = NULL;
    ql_net_socket_t* server = NULL;
    uint16_t port = 0;
    size_t moved = 1;
    char buffer[8];

    listener = make_listener(&port);
    MU_ASSERT(listener != NULL);
    MU_ASSERT(ql_net_tcp_connect(NULL, &addr, port, &client) == QL_OK);
    MU_ASSERT(ql_net_accept(listener, &server) == QL_OK);
    MU_ASSERT(ql_net_set_timeout(server, NET_TIMEOUT_MS) == QL_OK);

    ql_net_socket_close(client);

    /* An orderly shutdown is QL_OK with nothing read, not an error. */
    MU_CHECK(ql_net_recv(server, buffer, sizeof buffer, &moved) == QL_OK);
    MU_CHECK(moved == 0);

    ql_net_socket_close(server);
    ql_net_socket_close(listener);
}

MU_TEST(test_connect_to_a_closed_port_is_io)
{
    ql_net_addr_t addr = loopback_address();
    ql_net_socket_t* listener;
    ql_net_socket_t* client = NULL;
    uint16_t port = 0;

    /* Take a port, then give it back: nothing is listening on it now. */
    listener = make_listener(&port);
    MU_ASSERT(listener != NULL);
    ql_net_socket_close(listener);

    MU_CHECK(ql_net_tcp_connect(NULL, &addr, port, &client) == QL_ERR_IO);
    MU_CHECK(client == NULL);
}

MU_TEST(test_accept_times_out_instead_of_blocking)
{
    ql_net_socket_t* listener;
    ql_net_socket_t* server = NULL;
    uint16_t port = 0;

    listener = make_listener(&port);
    MU_ASSERT(listener != NULL);
    MU_ASSERT(ql_net_set_timeout(listener, 100) == QL_OK);

    /* Nothing is pending, so the wait expires -- and it must expire, not block.
     * qlib does the wait with select() precisely so this is the same answer
     * everywhere. */
    MU_CHECK(ql_net_accept(listener, &server) == QL_ERR_TIMEOUT);
    MU_CHECK(server == NULL);

    ql_net_socket_close(listener);
}

MU_TEST(test_recv_times_out_when_nothing_arrives)
{
    ql_net_addr_t addr = loopback_address();
    ql_net_socket_t* listener;
    ql_net_socket_t* client = NULL;
    ql_net_socket_t* server = NULL;
    uint16_t port = 0;
    size_t moved = 1;
    char buffer[8];

    listener = make_listener(&port);
    MU_ASSERT(listener != NULL);
    MU_ASSERT(ql_net_tcp_connect(NULL, &addr, port, &client) == QL_OK);
    MU_ASSERT(ql_net_accept(listener, &server) == QL_OK);
    MU_ASSERT(ql_net_set_timeout(server, 100) == QL_OK);

    /* The client sent nothing, so the wait expires and says so. */
    MU_CHECK(ql_net_recv(server, buffer, sizeof buffer, &moved) == QL_ERR_TIMEOUT);
    MU_CHECK(moved == 0);

    /* And the socket is still usable afterwards: the peer can still send. */
    MU_ASSERT(ql_net_send(client, "ok", 2, &moved) == QL_OK);
    MU_ASSERT(ql_net_set_timeout(server, NET_TIMEOUT_MS) == QL_OK);
    MU_ASSERT(ql_net_recv(server, buffer, sizeof buffer, &moved) == QL_OK);
    MU_CHECK(moved == 2);

    ql_net_socket_close(server);
    ql_net_socket_close(client);
    ql_net_socket_close(listener);
}

MU_TEST(test_socket_rejects_bad_arguments)
{
    ql_net_addr_t addr = loopback_address();
    ql_net_addr_t v6;
    ql_net_socket_t* sock = (ql_net_socket_t*)&g_probe;
    size_t moved = 0;
    char buffer[4];
    uint16_t port = 0;

    memset(&v6, 0, sizeof v6);
    v6.family = QL_NET_AF_INET6;

    MU_CHECK(ql_net_tcp_listen(NULL, NULL, 0, 1, &sock) == QL_ERR_INVALID_ARG);
    MU_CHECK(sock == NULL);
    MU_CHECK(ql_net_tcp_listen(NULL, &addr, 0, 1, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_tcp_connect(NULL, NULL, 0, &sock) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_accept(NULL, &sock) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_send(NULL, "x", 1, &moved) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_recv(NULL, buffer, sizeof buffer, &moved) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_set_timeout(NULL, 10) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_net_local_port(NULL, &port) == QL_ERR_INVALID_ARG);

    /* IPv6 is refused rather than half-attempted. */
    MU_CHECK(ql_net_tcp_listen(NULL, &v6, 0, 1, &sock) == QL_ERR_NOT_SUPPORTED);
    MU_CHECK(ql_net_tcp_connect(NULL, &v6, 0, &sock) == QL_ERR_NOT_SUPPORTED);
}

MU_TEST(test_sockets_do_not_leak)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_net_addr_t addr = loopback_address();
    ql_net_socket_t* listener = NULL;
    ql_net_socket_t* client = NULL;
    ql_net_socket_t* server = NULL;
    uint16_t port = 0;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    MU_ASSERT(ql_net_tcp_listen(&alloc, &addr, 0, 4, &listener) == QL_OK);
    MU_ASSERT(ql_net_local_port(listener, &port) == QL_OK);
    MU_ASSERT(ql_net_tcp_connect(&alloc, &addr, port, &client) == QL_OK);
    MU_ASSERT(ql_net_accept(listener, &server) == QL_OK);

    /* One control block per socket, nothing else. */
    MU_CHECK(state.live_blocks == 3);

    ql_net_socket_close(server);
    ql_net_socket_close(client);
    ql_net_socket_close(listener);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST_SUITE(test_suite_net)
{
    MU_RUN_TEST(test_parse_accepts_valid_addresses);
    MU_RUN_TEST(test_parse_zeroes_the_whole_struct);
    MU_RUN_TEST(test_round_trips);
    MU_RUN_TEST(test_parse_rejects_malformed_text);
    MU_RUN_TEST(test_parse_rejects_ambiguous_leading_zeros);
    MU_RUN_TEST(test_parse_rejects_out_of_range_octets);
    MU_RUN_TEST(test_parse_reports_ipv6_as_unsupported);
    MU_RUN_TEST(test_parse_rejects_null_arguments);
    MU_RUN_TEST(test_format_writes_the_address);
    MU_RUN_TEST(test_format_rejects_short_buffers_without_writing);
    MU_RUN_TEST(test_format_rejects_bad_arguments_and_families);
    MU_RUN_TEST(test_equality);
    MU_RUN_TEST(test_failures_are_raised_not_just_returned);
    MU_RUN_TEST(test_resolve_numeric_and_local_names);
    MU_RUN_TEST(test_connect_by_resolved_name);
    MU_RUN_TEST(test_listen_picks_a_port);
    MU_RUN_TEST(test_loopback_round_trip);
    MU_RUN_TEST(test_recv_reports_a_closed_peer_as_zero_bytes);
    MU_RUN_TEST(test_connect_to_a_closed_port_is_io);
    MU_RUN_TEST(test_accept_times_out_instead_of_blocking);
    MU_RUN_TEST(test_recv_times_out_when_nothing_arrives);
    MU_RUN_TEST(test_socket_rejects_bad_arguments);
    MU_RUN_TEST(test_sockets_do_not_leak);
}
