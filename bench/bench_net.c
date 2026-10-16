#include "bench.h"

#include "qlib/ql_net.h"

#include <string.h>

/*
 * Network module benchmarks.
 *
 * Parsing is the kind of thing that ends up in a configuration-loading loop, so
 * its cost is worth a number. Both cases check the result they measure.
 */

#define QLIB_BENCH_NET_ITERS 1000000U
#define QLIB_BENCH_NET_TEXT "203.0.113.7"

/** Parsed once so the format benchmark does not pay for parsing too. */
static ql_net_addr_t g_addr;

static void bench_parse_run(size_t n)
{
    size_t i;
    unsigned failed = 0;

    for (i = 0; i < n; i++)
    {
        ql_net_addr_t addr;
        failed |= (unsigned)(ql_net_addr_parse(QLIB_BENCH_NET_TEXT, &addr) != QL_OK);
        ql_bench_sink((uintptr_t)addr.bytes[3]);
    }

    QL_BENCH_CHECK(failed == 0, "parse");
}

static void bench_format_setup(size_t n)
{
    (void)n;
    QL_BENCH_CHECK(ql_net_addr_parse(QLIB_BENCH_NET_TEXT, &g_addr) == QL_OK, "setup parse");
}

static void bench_format_run(size_t n)
{
    char buf[QL_NET_ADDR_STR_MAX];
    size_t i;
    unsigned failed = 0;

    for (i = 0; i < n; i++)
    {
        failed |= (unsigned)(ql_net_addr_format(&g_addr, buf, sizeof buf) != QL_OK);
        ql_bench_sink((uintptr_t)buf[0]);
    }

    QL_BENCH_CHECK(failed == 0, "format");
    QL_BENCH_CHECK(strcmp(buf, QLIB_BENCH_NET_TEXT) == 0, "round trip");
}

static const ql_bench_case g_net_cases[] = {
    {"net_addr_parse", QLIB_BENCH_NET_ITERS, NULL, bench_parse_run, NULL},
    {"net_addr_format", QLIB_BENCH_NET_ITERS, bench_format_setup, bench_format_run, NULL},
};

const ql_bench_suite ql_bench_suite_net = {
    "net",
    g_net_cases,
    sizeof g_net_cases / sizeof g_net_cases[0],
};
