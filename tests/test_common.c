#include "test_suites.h"

#include "qlib/ql_common.h"

#include <errno.h>
#include <string.h>

/* Every enumerator, kept in one place so a new code cannot be forgotten. */
static const ql_ret_t k_all_codes[] = {
    QL_OK,
    QL_ERR_NOMEM,
    QL_ERR_INVALID_ARG,
    QL_ERR_OUT_OF_RANGE,
    QL_ERR_EMPTY,
    QL_ERR_NOT_FOUND,
    QL_ERR_IO,
    QL_ERR_SYS,
    QL_ERR_AGAIN,
    QL_ERR_TIMEOUT,
    QL_ERR_BUSY,
    QL_ERR_EXISTS,
    QL_ERR_NOT_SUPPORTED,
    QL_ERR_OVERFLOW,
};

static void dummy_handler(ql_ret_t code, const char* message, void* userdata)
{
    (void)code;
    (void)message;
    (void)userdata;
}

MU_TEST(test_version_matches_header)
{
    /* MU_ASSERT, not MU_CHECK: the next line dereferences the pointer. */
    MU_ASSERT(ql_version() != NULL);
    MU_CHECK(strcmp(ql_version(), QLIB_VERSION_STRING) == 0);
}

MU_TEST(test_strerror_covers_every_code)
{
    const size_t count = sizeof k_all_codes / sizeof k_all_codes[0];
    size_t i;
    size_t j;

    for (i = 0; i < count; i++)
    {
        const char* msg = ql_strerror(k_all_codes[i]);
        MU_ASSERT(msg != NULL);
        MU_CHECK(strlen(msg) > 0);
        MU_CHECK(strcmp(msg, "unknown error") != 0);

        /* Codes must be distinguishable from one another. */
        for (j = 0; j < i; j++)
        {
            MU_CHECK(strcmp(ql_strerror(k_all_codes[j]), msg) != 0);
        }
    }
}

MU_TEST(test_strerror_out_of_range)
{
    /*
     * Casting out of the enum's range is the point of this test: ql_strerror
     * must stay total for a value that came from, say, a corrupted struct.
     */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- intentional
    MU_CHECK(strcmp(ql_strerror((ql_ret_t)9999), "unknown error") == 0);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- intentional
    MU_CHECK(strcmp(ql_strerror((ql_ret_t)-1), "unknown error") == 0);
}

MU_TEST(test_ret_from_errno)
{
    MU_CHECK(ql_ret_from_errno(0) == QL_OK);
    MU_CHECK(ql_ret_from_errno(ENOMEM) == QL_ERR_NOMEM);
    MU_CHECK(ql_ret_from_errno(EINVAL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_ret_from_errno(ERANGE) == QL_ERR_OUT_OF_RANGE);
    MU_CHECK(ql_ret_from_errno(ENOENT) == QL_ERR_NOT_FOUND);
    MU_CHECK(ql_ret_from_errno(EIO) == QL_ERR_IO);
#ifdef EEXIST
    MU_CHECK(ql_ret_from_errno(EEXIST) == QL_ERR_EXISTS);
#endif
#ifdef EAGAIN
    MU_CHECK(ql_ret_from_errno(EAGAIN) == QL_ERR_AGAIN);
#endif
#ifdef ETIMEDOUT
    MU_CHECK(ql_ret_from_errno(ETIMEDOUT) == QL_ERR_TIMEOUT);
#endif
#ifdef EBUSY
    MU_CHECK(ql_ret_from_errno(EBUSY) == QL_ERR_BUSY);
#endif
#ifdef EOVERFLOW
    MU_CHECK(ql_ret_from_errno(EOVERFLOW) == QL_ERR_OVERFLOW);
#endif
#ifdef ENOTSUP
    MU_CHECK(ql_ret_from_errno(ENOTSUP) == QL_ERR_NOT_SUPPORTED);
#endif

    /* Anything unmapped must land on QL_ERR_SYS rather than being ignored. */
    MU_CHECK(ql_ret_from_errno(2147483647) == QL_ERR_SYS);
}

MU_TEST(test_error_handler_registry)
{
    ql_err_cb cb = dummy_handler;
    void* userdata = (void*)&cb; /* explicit: &cb is a pointer to a function pointer */
    int token = 0;

    ql_set_error_handler(NULL, NULL);
    ql_get_error_handler(&cb, &userdata);
    MU_CHECK(cb == NULL);
    MU_CHECK(userdata == NULL);

    ql_set_error_handler(dummy_handler, &token);
    ql_get_error_handler(&cb, &userdata);
    MU_CHECK(cb == dummy_handler);
    MU_CHECK(userdata == &token);

    /* Either output pointer may be NULL. */
    ql_get_error_handler(NULL, NULL);
    cb = NULL;
    ql_get_error_handler(&cb, NULL);
    MU_CHECK(cb == dummy_handler);
    userdata = NULL;
    ql_get_error_handler(NULL, &userdata);
    MU_CHECK(userdata == &token);

    ql_set_error_handler(NULL, NULL);
}

MU_TEST_SUITE(test_suite_common)
{
    MU_RUN_TEST(test_version_matches_header);
    MU_RUN_TEST(test_strerror_covers_every_code);
    MU_RUN_TEST(test_strerror_out_of_range);
    MU_RUN_TEST(test_ret_from_errno);
    MU_RUN_TEST(test_error_handler_registry);
}
