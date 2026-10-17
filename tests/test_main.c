#include "test_suites.h"

int main(void)
{
    MU_RUN_SUITE(test_suite_common);
    MU_RUN_SUITE(test_suite_hash);
    MU_RUN_SUITE(test_suite_list);
    MU_RUN_SUITE(test_suite_mem);
    MU_RUN_SUITE(test_suite_str);
    MU_RUN_SUITE(test_suite_vector);
#ifdef QLIB_HAS_NET
    MU_RUN_SUITE(test_suite_net);
#endif
#ifdef QLIB_HAS_THREADS
    MU_RUN_SUITE(test_suite_thread);
#endif
    MU_REPORT();
    return MU_EXIT_CODE;
}
