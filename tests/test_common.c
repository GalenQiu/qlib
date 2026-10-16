#include "test_suites.h"

#include "qlib/ql_common.h"

MU_TEST(test_default_allocator)
{
    ql_allocator_t al = ql_default_allocator();
    MU_CHECK(al.malloc != NULL);
    MU_CHECK(al.realloc != NULL);
    MU_CHECK(al.free != NULL);
    MU_CHECK(al.userdata == NULL);

    void* p = al.malloc(64, al.userdata);
    MU_ASSERT(p != NULL);

    void* p2 = al.realloc(p, 128, al.userdata);
    if (p2 == NULL)
    {
        al.free(p, al.userdata);
    }
    MU_ASSERT(p2 != NULL);

    /* free(NULL) must be a no-op, like libc free. */
    al.free(NULL, al.userdata);
    al.free(p2, al.userdata);
}

MU_TEST_SUITE(test_suite_common)
{
    MU_RUN_TEST(test_default_allocator);
}
