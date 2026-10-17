#ifndef QLIB_BENCH_H
#define QLIB_BENCH_H

#include <stddef.h>
#include <stdint.h>

/*
 * Minimal benchmark harness, internal to bench/.
 *
 * There is no external framework on purpose: qlib has no dependencies, and a
 * benchmark that pulls one in would be the only thing in the tree that does.
 *
 * A case declares an operation count, an optional untimed setup, the timed
 * region, and an optional teardown. Setup and teardown exist so that building
 * the input is not charged to the operation being measured -- the difference
 * between push_back with and without a reserve is invisible otherwise.
 */

typedef struct
{
    const char* name;
    /** Operation count before --scale is applied. */
    size_t iterations;
    /** Optional; runs before timing starts. */
    void (*setup)(size_t n);
    /** Required; this is the region that gets timed. */
    void (*run)(size_t n);
    /** Optional; runs after timing stops. */
    void (*teardown)(void);
} ql_bench_case;

typedef struct
{
    const char* name;
    const ql_bench_case* cases;
    size_t count;
} ql_bench_suite;

/* Each suite translation unit provides one of these. */
extern const ql_bench_suite ql_bench_suite_vector;
extern const ql_bench_suite ql_bench_suite_mem;
extern const ql_bench_suite ql_bench_suite_str;
extern const ql_bench_suite ql_bench_suite_hash;
extern const ql_bench_suite ql_bench_suite_list;

/* Present only when the optional network module was built. */
#ifdef QLIB_HAS_NET
extern const ql_bench_suite ql_bench_suite_net;
#endif

/**
 * @brief Consume a value so the optimiser cannot delete the work that made it.
 */
void ql_bench_sink(uintptr_t value);

/**
 * @brief Report a failed correctness check. Never returns.
 */
_Noreturn void ql_bench_fail(const char* what, const char* file, int line);

/**
 * @brief Fail loudly if a benchmark's own correctness check does not hold.
 *
 * A benchmark that measures broken code produces a number nobody should trust,
 * so these are hard failures rather than warnings.
 *
 * This is a macro rather than a function so that the compiler and the static
 * analyser can both see that control does not continue when the check fails.
 * Behind a function call, clang-tidy reports a null dereference on the very
 * next line of code that the check was there to make safe.
 */
#define QL_BENCH_CHECK(condition, what)                \
    do                                                 \
    {                                                  \
        if (!(condition))                              \
        {                                              \
            ql_bench_fail((what), __FILE__, __LINE__); \
        }                                              \
    } while (0)

#endif /* QLIB_BENCH_H */
