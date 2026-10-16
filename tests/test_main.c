#include "test_suites.h"

int main(void)
{
    MU_RUN_SUITE(test_suite_common);
    MU_REPORT();
    return MU_EXIT_CODE;
}
