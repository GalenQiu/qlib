#include "bench.h"

#include "qlib/ql_mem.h"

#include <stdint.h>

/*
 * Memory benchmarks.
 *
 * These measure the helpers, not libc: the interesting comparison is
 * ql_aligned_alloc() against plain ql_alloc(), because the aligned path is the
 * one that pays the documented alignment + sizeof(void*) overhead.
 */

#define QLIB_BENCH_ALLOC_ITERS 1000000U
#define QLIB_BENCH_ALLOC_SIZE 64U
#define QLIB_BENCH_ALIGNMENT 64U

static void bench_alloc_free_run(size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
    {
        unsigned char* p = ql_alloc(NULL, QLIB_BENCH_ALLOC_SIZE);

        QL_BENCH_CHECK(p != NULL, "ql_alloc");
        /* Touch both ends so the allocation cannot be folded away. */
        p[0] = (unsigned char)i;
        p[QLIB_BENCH_ALLOC_SIZE - 1U] = (unsigned char)i;
        ql_bench_sink((uintptr_t)p[0] ^ (uintptr_t)p[QLIB_BENCH_ALLOC_SIZE - 1U]);

        ql_dealloc(NULL, p);
    }
}

static void bench_aligned_alloc_free_run(size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
    {
        unsigned char* p = ql_aligned_alloc(NULL, QLIB_BENCH_ALIGNMENT, QLIB_BENCH_ALLOC_SIZE);

        QL_BENCH_CHECK(p != NULL, "ql_aligned_alloc");
        QL_BENCH_CHECK(((uintptr_t)p % (uintptr_t)QLIB_BENCH_ALIGNMENT) == 0U, "alignment");

        p[0] = (unsigned char)i;
        p[QLIB_BENCH_ALLOC_SIZE - 1U] = (unsigned char)i;
        ql_bench_sink((uintptr_t)p[0] ^ (uintptr_t)p[QLIB_BENCH_ALLOC_SIZE - 1U]);

        ql_aligned_free(NULL, p);
    }
}

/** @brief The multiplication guard on its own, with operands that never fail. */
static void bench_size_mul_overflow_run(size_t n)
{
    size_t i;
    uintptr_t sum = 0;

    for (i = 0; i < n; i++)
    {
        size_t total = 0;
        if (ql_size_mul_overflow(i, sizeof(void*), &total) != QL_OK)
        {
            QL_BENCH_CHECK(0, "ql_size_mul_overflow");
        }
        sum += total;
    }

    ql_bench_sink(sum);
}

static const ql_bench_case g_mem_cases[] = {
    {"mem_alloc_free", QLIB_BENCH_ALLOC_ITERS, NULL, bench_alloc_free_run, NULL},
    {"mem_aligned_alloc_free", QLIB_BENCH_ALLOC_ITERS, NULL, bench_aligned_alloc_free_run, NULL},
    {"mem_size_mul_overflow", QLIB_BENCH_ALLOC_ITERS, NULL, bench_size_mul_overflow_run, NULL},
};

const ql_bench_suite ql_bench_suite_mem = {
    "mem",
    g_mem_cases,
    sizeof g_mem_cases / sizeof g_mem_cases[0],
};
