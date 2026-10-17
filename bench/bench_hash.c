#include "bench.h"

#include "qlib/ql_hash.h"

/*
 * Hash table benchmarks.
 *
 * Insertion includes growth and rehashing, which is what a caller actually
 * pays. Lookup runs against a table built in setup, so it measures probing and
 * key comparison rather than construction.
 */

#define QLIB_BENCH_HASH_INSERT_ITERS 100000U
#define QLIB_BENCH_HASH_LOOKUP_ITERS 1000000U
#define QLIB_BENCH_HASH_FIXED 4096U

static ql_hash_t* g_hash;

static void insert_setup(size_t n)
{
    (void)n;
    QL_BENCH_CHECK(ql_hash_create(NULL, sizeof(uint32_t), sizeof(uint32_t), &g_hash) == QL_OK, "create");
}

static void insert_teardown(void)
{
    ql_hash_destroy(g_hash);
    g_hash = NULL;
}

static void hash_insert_run(size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
    {
        uint32_t key = (uint32_t)i;
        uint32_t value = (uint32_t)i * 7U;

        QL_BENCH_CHECK(ql_hash_set(g_hash, &key, &value) == QL_OK, "insert");
    }

    QL_BENCH_CHECK(ql_hash_size(g_hash) == n, "entry count after inserts");
    ql_bench_sink((uintptr_t)ql_hash_capacity(g_hash));
}

static void lookup_setup(size_t n)
{
    uint32_t i;

    (void)n;
    QL_BENCH_CHECK(ql_hash_create(NULL, sizeof(uint32_t), sizeof(uint32_t), &g_hash) == QL_OK, "create");
    for (i = 0; i < QLIB_BENCH_HASH_FIXED; i++)
    {
        uint32_t value = i * 3U;

        QL_BENCH_CHECK(ql_hash_set(g_hash, &i, &value) == QL_OK, "preload");
    }
}

static void hash_lookup_run(size_t n)
{
    size_t i;
    unsigned failed = 0;

    for (i = 0; i < n; i++)
    {
        uint32_t key = (uint32_t)(i % QLIB_BENCH_HASH_FIXED);
        uint32_t out = 0;

        failed |= (unsigned)(ql_hash_get(g_hash, &key, &out) != QL_OK);
        failed |= (unsigned)(out != key * 3U);
    }

    QL_BENCH_CHECK(failed == 0, "every lookup hit and matched");
}

static const ql_bench_case g_hash_cases[] = {
    {"hash_insert", QLIB_BENCH_HASH_INSERT_ITERS, insert_setup, hash_insert_run, insert_teardown},
    {"hash_lookup", QLIB_BENCH_HASH_LOOKUP_ITERS, lookup_setup, hash_lookup_run, insert_teardown},
};

const ql_bench_suite ql_bench_suite_hash = {
    "hash",
    g_hash_cases,
    sizeof g_hash_cases / sizeof g_hash_cases[0],
};
