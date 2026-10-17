#include "bench.h"

#include "qlib/ql_str.h"

#include <string.h>

/*
 * String benchmarks.
 *
 * Appending a character measures the amortised growth path; appending formatted
 * text measures the vsnprintf retry loop, which is the expensive part of the
 * module and the reason it is worth having a number for.
 */

#define QLIB_BENCH_STR_ITERS 1000000U
#define QLIB_BENCH_STR_PRINTF_ITERS 200000U

static ql_str_t* g_str;

static void str_setup(size_t n)
{
    (void)n;
    QL_BENCH_CHECK(ql_str_create(NULL, &g_str) == QL_OK, "create");
}

static void str_teardown(void)
{
    ql_str_destroy(g_str);
    g_str = NULL;
}

static void str_append_char_run(size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
    {
        QL_BENCH_CHECK(ql_str_append_char(g_str, 'x') == QL_OK, "append char");
    }

    QL_BENCH_CHECK(ql_str_size(g_str) == n, "length after appends");
    ql_bench_sink((uintptr_t)ql_str_cstr(g_str)[0]);
    ql_str_clear(g_str);
}

static void str_append_printf_run(size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
    {
        QL_BENCH_CHECK(ql_str_append_printf(g_str, "%zu,", i) == QL_OK, "append printf");
    }

    QL_BENCH_CHECK(ql_str_size(g_str) > 0, "printf produced output");
    /* The last append must still have left a valid C string. */
    QL_BENCH_CHECK(ql_str_cstr(g_str)[ql_str_size(g_str)] == '\0', "terminated");
    ql_bench_sink((uintptr_t)ql_str_cstr(g_str)[0]);
    ql_str_clear(g_str);
}

static const ql_bench_case g_str_cases[] = {
    {"str_append_char", QLIB_BENCH_STR_ITERS, str_setup, str_append_char_run, str_teardown},
    {"str_append_printf", QLIB_BENCH_STR_PRINTF_ITERS, str_setup, str_append_printf_run, str_teardown},
};

const ql_bench_suite ql_bench_suite_str = {
    "str",
    g_str_cases,
    sizeof g_str_cases / sizeof g_str_cases[0],
};
