#include "harness_suites.h"

/*
 * Runner for the deliberately failing probe suite. Lives in its own
 * translation unit so that the counters must genuinely cross a TU boundary,
 * which is exactly the property being tested.
 */
int main(void)
{
    MU_RUN_SUITE(suite_intentional_failure);
    MU_REPORT();
    return MU_EXIT_CODE;
}
