#include "bench.h"

#include "qlib/ql_vector.h"

#include <stdint.h>

/*
 * Vector benchmarks.
 *
 * Every one of them checks the result it is about to report a number for: a
 * benchmark of broken code produces a figure nobody should act on. The checks
 * are deliberately outside the hot loop where they would perturb the
 * measurement, and the loop body consumes each return value with a branch-free
 * `|=`, which is enough to keep the optimiser honest without adding a
 * predictable branch per iteration.
 */

#define QLIB_BENCH_MODERATE 1000000U
#define QLIB_BENCH_QUADRATIC 2000U

/** Vector used by the benchmarks that need pre-built input. */
static ql_vector_t* g_vec;

/* ---------------------------------------------------------------------------
 * push_back, growing from nothing
 * ------------------------------------------------------------------------ */

static void bench_push_back_growing_run(size_t n)
{
    ql_vector_t* vec = NULL;
    size_t i;
    unsigned failed = 0;

    QL_BENCH_CHECK(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK, "create");

    for (i = 0; i < n; i++)
    {
        int value = (int)i;
        failed |= (unsigned)(ql_vector_push_back(vec, &value) != QL_OK);
    }

    QL_BENCH_CHECK(failed == 0, "push_back");
    QL_BENCH_CHECK(ql_vector_size(vec) == n, "size after push_back");
    ql_bench_sink((uintptr_t)ql_vector_size(vec));
    ql_bench_sink((uintptr_t)ql_vector_capacity(vec));

    ql_vector_destroy(vec);
}

/* ---------------------------------------------------------------------------
 * push_back with capacity pre-reserved
 *
 * Setup is untimed on purpose: the whole point is to show the cost of
 * push_back once the reallocations are out of the way.
 * ------------------------------------------------------------------------ */

static void bench_push_back_reserved_setup(size_t n)
{
    QL_BENCH_CHECK(ql_vector_create(NULL, sizeof(int), &g_vec) == QL_OK, "create");
    QL_BENCH_CHECK(ql_vector_reserve(g_vec, n) == QL_OK, "reserve");
}

static void bench_push_back_reserved_run(size_t n)
{
    size_t i;
    unsigned failed = 0;

    for (i = 0; i < n; i++)
    {
        int value = (int)i;
        failed |= (unsigned)(ql_vector_push_back(g_vec, &value) != QL_OK);
    }

    QL_BENCH_CHECK(failed == 0, "push_back");
    QL_BENCH_CHECK(ql_vector_size(g_vec) == n, "size after push_back");
    ql_bench_sink((uintptr_t)ql_vector_size(g_vec));
}

static void bench_push_back_reserved_teardown(void)
{
    ql_vector_destroy(g_vec);
    g_vec = NULL;
}

/* ---------------------------------------------------------------------------
 * Summing the elements
 * ------------------------------------------------------------------------ */

static void bench_iterate_setup(size_t n)
{
    size_t i;

    QL_BENCH_CHECK(ql_vector_create(NULL, sizeof(int), &g_vec) == QL_OK, "create");
    QL_BENCH_CHECK(ql_vector_reserve(g_vec, n) == QL_OK, "reserve");
    for (i = 0; i < n; i++)
    {
        int value = (int)i;
        QL_BENCH_CHECK(ql_vector_push_back(g_vec, &value) == QL_OK, "fill");
    }
}

static void bench_iterate_run(size_t n)
{
    uintptr_t sum = 0;
    size_t i;

    for (i = 0; i < n; i++)
    {
        const int* value = ql_vector_at(g_vec, i);
        QL_BENCH_CHECK(value != NULL, "at");
        sum += (uintptr_t)*value;
    }

    ql_bench_sink(sum);
}

static void bench_iterate_teardown(void)
{
    ql_vector_destroy(g_vec);
    g_vec = NULL;
}

/* ---------------------------------------------------------------------------
 * insert() and remove() at the front: the quadratic cases
 *
 * The counts are small because both are O(n) per call. They are here to make
 * that cost visible next to push_back rather than to flatter the container.
 * ------------------------------------------------------------------------ */

static void bench_insert_front_run(size_t n)
{
    ql_vector_t* vec = NULL;
    size_t i;
    unsigned failed = 0;

    QL_BENCH_CHECK(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK, "create");

    for (i = 0; i < n; i++)
    {
        int value = (int)i;
        failed |= (unsigned)(ql_vector_insert(vec, 0, &value) != QL_OK);
    }

    QL_BENCH_CHECK(failed == 0, "insert");
    QL_BENCH_CHECK(ql_vector_size(vec) == n, "size after insert");
    ql_bench_sink((uintptr_t)ql_vector_size(vec));

    ql_vector_destroy(vec);
}

static void bench_remove_front_setup(size_t n)
{
    size_t i;

    QL_BENCH_CHECK(ql_vector_create(NULL, sizeof(int), &g_vec) == QL_OK, "create");
    QL_BENCH_CHECK(ql_vector_reserve(g_vec, n) == QL_OK, "reserve");
    for (i = 0; i < n; i++)
    {
        int value = (int)i;
        QL_BENCH_CHECK(ql_vector_push_back(g_vec, &value) == QL_OK, "fill");
    }
}

static void bench_remove_front_run(size_t n)
{
    size_t i;
    unsigned failed = 0;

    for (i = 0; i < n; i++)
    {
        failed |= (unsigned)(ql_vector_remove(g_vec, 0) != QL_OK);
    }

    QL_BENCH_CHECK(failed == 0, "remove");
    QL_BENCH_CHECK(ql_vector_empty(g_vec), "empty after remove");
    ql_bench_sink((uintptr_t)ql_vector_size(g_vec));
}

static void bench_remove_front_teardown(void)
{
    ql_vector_destroy(g_vec);
    g_vec = NULL;
}

/* ---------------------------------------------------------------------------
 * Suite
 * ------------------------------------------------------------------------ */

static const ql_bench_case g_vector_cases[] = {
    {"vector_push_back_growing", QLIB_BENCH_MODERATE, NULL, bench_push_back_growing_run, NULL},
    {
        "vector_push_back_reserved",
        QLIB_BENCH_MODERATE,
        bench_push_back_reserved_setup,
        bench_push_back_reserved_run,
        bench_push_back_reserved_teardown,
    },
    {"vector_iterate_sum", QLIB_BENCH_MODERATE, bench_iterate_setup, bench_iterate_run, bench_iterate_teardown},
    {"vector_insert_front", QLIB_BENCH_QUADRATIC, NULL, bench_insert_front_run, NULL},
    {
        "vector_remove_front",
        QLIB_BENCH_QUADRATIC,
        bench_remove_front_setup,
        bench_remove_front_run,
        bench_remove_front_teardown,
    },
};

const ql_bench_suite ql_bench_suite_vector = {
    "vector",
    g_vector_cases,
    sizeof g_vector_cases / sizeof g_vector_cases[0],
};
