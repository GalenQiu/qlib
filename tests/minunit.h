#ifndef QLIB_MINUNIT_H
#define QLIB_MINUNIT_H

#include <stdio.h>
#include <stdlib.h>

/*
 * The counters have a single definition in minunit.c and are shared by every
 * translation unit. Defining them as `static` here would give each .c file its
 * own copy, so a failing MU_CHECK in a test file would never be visible to the
 * MU_REPORT/MU_EXIT_CODE in test_main.c.
 */
extern int mu_tests_run;
extern int mu_failures;

/* Test case: a static function body. */
#define MU_TEST(name) static void name(void)

/* Test suite: a function holding MU_RUN_TEST calls. */
#define MU_TEST_SUITE(name) void name(void)

/*
 * Forward declaration of a suite, for the file that drives the run.
 * Use it instead of hand written prototypes so the linkage always matches the
 * MU_TEST_SUITE definition (a `static` prototype against an external
 * definition is undefined behaviour and warns under -Wmissing-prototypes).
 */
#define MU_DECLARE_SUITE(name) void name(void)

#define MU_RUN_TEST(test) \
    do                    \
    {                     \
        mu_tests_run++;   \
        test();           \
    } while (0)

#define MU_RUN_SUITE(suite) \
    do                      \
    {                       \
        suite();            \
    } while (0)

/* Record a failure and keep going. */
#define MU_CHECK(condition)                                                       \
    do                                                                            \
    {                                                                             \
        if (!(condition))                                                         \
        {                                                                         \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            mu_failures++;                                                        \
        }                                                                         \
    } while (0)

/*
 * Record a failure and abort the current test. Use this when the statements
 * that follow the check would dereference or index something invalid.
 */
#define MU_ASSERT(condition)                                                      \
    do                                                                            \
    {                                                                             \
        if (!(condition))                                                         \
        {                                                                         \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            mu_failures++;                                                        \
            return;                                                               \
        }                                                                         \
    } while (0)

#define MU_REPORT()                                                                \
    do                                                                             \
    {                                                                              \
        fprintf(stderr, "%d tests run, %d failures\n", mu_tests_run, mu_failures); \
    } while (0)

#define MU_EXIT_CODE (mu_failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS)

#endif /* QLIB_MINUNIT_H */
