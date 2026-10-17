#ifndef QLIB_TEST_SUITES_H
#define QLIB_TEST_SUITES_H

#include "minunit.h"

/*
 * Every suite defined with MU_TEST_SUITE must be declared here. Both the
 * defining file and the runner include this header, so the declaration and the
 * definition always agree, and -Wmissing-prototypes stays quiet.
 */

MU_DECLARE_SUITE(test_suite_common);
MU_DECLARE_SUITE(test_suite_hash);
MU_DECLARE_SUITE(test_suite_list);
MU_DECLARE_SUITE(test_suite_mem);
MU_DECLARE_SUITE(test_suite_str);
MU_DECLARE_SUITE(test_suite_vector);
#ifdef QLIB_HAS_NET
MU_DECLARE_SUITE(test_suite_net);
#endif

#ifdef QLIB_HAS_THREADS
MU_DECLARE_SUITE(test_suite_thread);
#endif

#endif /* QLIB_TEST_SUITES_H */
