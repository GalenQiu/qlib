#include "bench.h"

#include "qlib/ql_list.h"

/*
 * List benchmarks.
 *
 * Pushing measures allocating and linking a node, which is the cost that makes
 * a list lose to ql_vector for plain appending. Removing through a saved handle
 * measures what the list is actually for: unlinking and freeing one node with
 * no search.
 */

#define QLIB_BENCH_LIST_PUSH_ITERS 200000U
#define QLIB_BENCH_LIST_REMOVE_N 65536U

static ql_list_t* g_list;
/* Static rather than allocated: the harness has no heap helpers of its own,
 * and half a megabyte of BSS is cheaper than a lifetime to reason about. */
static ql_list_node_t* g_handles[QLIB_BENCH_LIST_REMOVE_N];

static void push_setup(size_t n)
{
    (void)n;
    QL_BENCH_CHECK(ql_list_create(NULL, sizeof(uint32_t), &g_list) == QL_OK, "create");
}

static void push_teardown(void)
{
    ql_list_destroy(g_list);
    g_list = NULL;
}

static void list_push_back_run(size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
    {
        uint32_t value = (uint32_t)i;

        QL_BENCH_CHECK(ql_list_push_back(g_list, &value, NULL) == QL_OK, "push");
    }

    QL_BENCH_CHECK(ql_list_size(g_list) == n, "size after pushes");
    ql_bench_sink((uintptr_t)ql_list_size(g_list));
    ql_list_clear(g_list);
}

static void remove_setup(size_t n)
{
    uint32_t i;

    (void)n;
    QL_BENCH_CHECK(ql_list_create(NULL, sizeof(uint32_t), &g_list) == QL_OK, "create");

    for (i = 0; i < QLIB_BENCH_LIST_REMOVE_N; i++)
    {
        uint32_t value = i;

        QL_BENCH_CHECK(ql_list_push_back(g_list, &value, &g_handles[i]) == QL_OK, "preload");
    }
}

static void remove_teardown(void)
{
    ql_list_destroy(g_list);
    g_list = NULL;
}

static void list_remove_run(size_t n)
{
    size_t i;
    size_t limit = n < QLIB_BENCH_LIST_REMOVE_N ? n : QLIB_BENCH_LIST_REMOVE_N;

    for (i = 0; i < limit; i++)
    {
        QL_BENCH_CHECK(ql_list_remove(g_list, g_handles[i]) == QL_OK, "remove");
    }

    QL_BENCH_CHECK(ql_list_size(g_list) == QLIB_BENCH_LIST_REMOVE_N - limit, "size after removals");
    ql_bench_sink((uintptr_t)ql_list_size(g_list));
    /* Anything left is freed by teardown. */
}

static const ql_bench_case g_list_cases[] = {
    {"list_push_back", QLIB_BENCH_LIST_PUSH_ITERS, push_setup, list_push_back_run, push_teardown},
    {"list_remove_handle", QLIB_BENCH_LIST_REMOVE_N, remove_setup, list_remove_run, remove_teardown},
};

const ql_bench_suite ql_bench_suite_list = {
    "list",
    g_list_cases,
    sizeof g_list_cases / sizeof g_list_cases[0],
};
