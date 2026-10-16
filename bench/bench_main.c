#ifndef _WIN32
/* clock_gettime() and CLOCK_MONOTONIC are POSIX, and -std=c11 hides both
 * without this. It has to be defined before any system header is pulled in.
 *
 * Feature-test macros are the sanctioned way to ask for declarations a strict
 * language mode withholds, which is why the reserved-identifier check is
 * silenced here rather than the macro being avoided. */
#  ifndef _POSIX_C_SOURCE
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include "bench.h"

#include "qlib/ql_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#endif

/* Written by every benchmark so the optimiser cannot delete the measured work. */
static volatile uintptr_t g_sink;

void ql_bench_sink(uintptr_t value)
{
    g_sink ^= value;
}

_Noreturn void ql_bench_fail(const char* what, const char* file, int line)
{
    fprintf(stderr, "bench: correctness check failed: %s (%s:%d)\n", what, file, line);
    exit(EXIT_FAILURE);
}

/* ---------------------------------------------------------------------------
 * Timer
 * ------------------------------------------------------------------------ */

#ifdef _WIN32

static double ql_bench_now(void)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

static const char* ql_bench_timer_name(void)
{
    return "QueryPerformanceCounter";
}

#else

static double ql_bench_now(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

static const char* ql_bench_timer_name(void)
{
    return "clock_gettime(CLOCK_MONOTONIC)";
}

#endif

/* ---------------------------------------------------------------------------
 * Runner
 * ------------------------------------------------------------------------ */

static size_t ql_bench_scaled(size_t iterations, double scale)
{
    size_t scaled = (size_t)((double)iterations * scale);

    return scaled == 0 ? 1 : scaled;
}

/** @brief Every suite, in the order they are reported. */
static const ql_bench_suite* const g_suites[] = {
    &ql_bench_suite_vector,
    &ql_bench_suite_mem,
};

static const char* ql_bench_standard(void)
{
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
    return "C23";
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201710L
    return "C17";
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    return "C11";
#else
    return "pre-C11";
#endif
}

static void ql_bench_print_header(void)
{
    printf("qlib %s benchmarks\n", ql_version());
    printf("  compiler   %s\n",
#ifdef __clang__
           "Clang"
#elif defined(_MSC_VER)
           "MSVC"
#elif defined(__GNUC__)
           "GCC"
#else
           "unknown"
#endif
    );
    printf("  standard   %s\n", ql_bench_standard());
    printf("  timer      %s\n", ql_bench_timer_name());
    printf("\n");
    printf("%-30s %10s %12s %12s\n", "benchmark", "n", "total", "per op");
    printf("%-30s %10s %12s %12s\n", "------------------------------", "----------", "------------", "------------");
}

static void ql_bench_run_case(const ql_bench_case* bench, double scale)
{
    size_t n = ql_bench_scaled(bench->iterations, scale);
    double start;
    double elapsed;
    double per_op;

    if (bench->setup != NULL)
    {
        bench->setup(n);
    }

    start = ql_bench_now();
    bench->run(n);
    elapsed = ql_bench_now() - start;

    if (bench->teardown != NULL)
    {
        bench->teardown();
    }

    per_op = elapsed / (double)n;

    /* Report in whichever unit keeps the number readable. */
    if (elapsed < 1e-3)
    {
        printf("%-30s %10zu %9.2f us %9.1f ns\n", bench->name, n, elapsed * 1e6, per_op * 1e9);
    }
    else
    {
        printf("%-30s %10zu %9.2f ms %9.1f ns\n", bench->name, n, elapsed * 1e3, per_op * 1e9);
    }
}

static void ql_bench_usage(const char* argv0)
{
    printf("usage: %s [--quick] [--list] [name ...]\n", argv0);
    printf("\n");
    printf("  --quick   run each benchmark with a tiny iteration count\n");
    printf("  --list    print the benchmark names and exit\n");
    printf("  name      run only the benchmarks whose name contains this text\n");
}

/** @brief Command line switches, resolved from argv. */
typedef struct
{
    double scale;
    int list_only;
    int show_help;
    int filters;
} ql_bench_options;

static void ql_bench_parse_options(int argc, char** argv, ql_bench_options* opts)
{
    int a;

    opts->scale = 1.0;
    opts->list_only = 0;
    opts->show_help = 0;
    opts->filters = 0;

    for (a = 1; a < argc; a++)
    {
        if (strcmp(argv[a], "--quick") == 0)
        {
            opts->scale = 0.001;
        }
        else if (strcmp(argv[a], "--list") == 0)
        {
            opts->list_only = 1;
        }
        else if (strcmp(argv[a], "--help") == 0 || strcmp(argv[a], "-h") == 0)
        {
            opts->show_help = 1;
        }
        else if (argv[a][0] != '-')
        {
            opts->filters++;
        }
    }
}

/** @brief Does this case match the name filters? Flags are not filters. */
static int ql_bench_selected(const ql_bench_case* bench, int argc, char** argv, int filters)
{
    int a;

    if (filters == 0)
    {
        return 1;
    }
    for (a = 1; a < argc; a++)
    {
        if (argv[a][0] != '-' && strstr(bench->name, argv[a]) != NULL)
        {
            return 1;
        }
    }
    return 0;
}

static void ql_bench_list(void)
{
    size_t s;

    for (s = 0; s < sizeof g_suites / sizeof g_suites[0]; s++)
    {
        size_t c;
        for (c = 0; c < g_suites[s]->count; c++)
        {
            printf("%s\n", g_suites[s]->cases[c].name);
        }
    }
}

static size_t ql_bench_total_cases(void)
{
    size_t total = 0;
    size_t s;

    for (s = 0; s < sizeof g_suites / sizeof g_suites[0]; s++)
    {
        total += g_suites[s]->count;
    }
    return total;
}

static size_t ql_bench_run_all(int argc, char** argv, const ql_bench_options* opts)
{
    size_t ran = 0;
    size_t s;

    for (s = 0; s < sizeof g_suites / sizeof g_suites[0]; s++)
    {
        size_t c;
        for (c = 0; c < g_suites[s]->count; c++)
        {
            const ql_bench_case* bench = &g_suites[s]->cases[c];

            if (ql_bench_selected(bench, argc, argv, opts->filters) != 0)
            {
                ql_bench_run_case(bench, opts->scale);
                ran++;
            }
        }
    }
    return ran;
}

int main(int argc, char** argv)
{
    ql_bench_options opts;
    size_t ran;

    ql_bench_parse_options(argc, argv, &opts);

    if (opts.show_help != 0)
    {
        ql_bench_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (opts.list_only != 0)
    {
        ql_bench_list();
        return EXIT_SUCCESS;
    }

    ql_bench_print_header();
    ran = ql_bench_run_all(argc, argv, &opts);

    printf("\n%zu of %zu cases\n", ran, ql_bench_total_cases());
    printf("sink 0x%llx\n", (unsigned long long)g_sink);

    if (ran == 0)
    {
        fprintf(stderr, "bench: no benchmark matched\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
