#ifndef QLIB_TEST_SUITES_H
#define QLIB_TEST_SUITES_H

#include "minunit.h"

/*
 * Every suite defined with MU_TEST_SUITE must be declared here. Both the
 * defining file and the runner include this header, so the declaration and the
 * definition always agree, and -Wmissing-prototypes stays quiet.
 */

MU_DECLARE_SUITE(test_suite_common);
MU_DECLARE_SUITE(test_suite_mem);
MU_DECLARE_SUITE(test_suite_vector);

#endif /* QLIB_TEST_SUITES_H */
