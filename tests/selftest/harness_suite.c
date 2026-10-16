#include "harness_suites.h"

/*
 * A test that is supposed to fail.
 *
 * This is not part of the real suite: it is linked into its own executable,
 * and ctest is told to expect a failure (see expect_failure.cmake). The point
 * is to guard the harness itself.
 *
 * The counter bug this project started with made a failing MU_CHECK invisible
 * to MU_REPORT/MU_EXIT_CODE, so the test binary exited 0 and CI stayed green.
 * If that ever regresses, this probe stops reporting its failure, exits 0, and
 * the ctest driver fails loudly.
 *
 * The comparison goes through variables so that no compiler's
 * -Wtautological-compare fires on a literal comparison under -Werror.
 */
MU_TEST(test_intentional_failure)
{
    int actual = 1;
    int expected = 2;

    MU_CHECK(actual == expected);
}

MU_TEST_SUITE(suite_intentional_failure)
{
    MU_RUN_TEST(test_intentional_failure);
}
