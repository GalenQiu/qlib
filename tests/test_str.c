#include "test_suites.h"

#include "qlib/ql_str.h"

#include <stdio.h>
#include <string.h>

#include "test_alloc.h"

/* ---------------------------------------------------------------------------
 * An error-handler probe, as in test_net: enough to prove that a failure is
 * raised as well as returned.
 * ------------------------------------------------------------------------ */

typedef struct
{
    int calls;
    ql_ret_t code;
} str_probe;

static str_probe g_probe;

static void str_probe_record(ql_ret_t code, const char* message, void* userdata)
{
    str_probe* probe = userdata;

    (void)message;
    probe->calls++;
    probe->code = code;
}

static void probe_begin(void)
{
    g_probe.calls = 0;
    g_probe.code = QL_OK;
    ql_set_error_handler(str_probe_record, &g_probe);
}

static void probe_expect(int calls, ql_ret_t code)
{
    if (g_probe.calls != calls)
    {
        fprintf(stderr, "  handler called %d time(s), expected %d\n", g_probe.calls, calls);
    }
    MU_CHECK(g_probe.calls == calls);
    MU_CHECK(g_probe.code == code);
}

/**
 * @brief Create a string holding @p text, or NULL on failure.
 *
 * Deliberately not using MU_ASSERT: it expands to a bare `return`, which a
 * function returning a pointer cannot use. Callers assert on the result, and
 * minunit reports the failure against the caller's line, which is the line that
 * actually needed the string.
 */
static ql_str_t* make_string(const char* text)
{
    ql_str_t* str = NULL;

    if (ql_str_create(NULL, &str) != QL_OK)
    {
        return NULL;
    }
    if (text != NULL && ql_str_append(str, text) != QL_OK)
    {
        ql_str_destroy(str);
        return NULL;
    }
    return str;
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

MU_TEST(test_create_rejects_bad_arguments)
{
    ql_str_t* str = (ql_str_t*)&g_probe;

    MU_CHECK(ql_str_create(NULL, NULL) == QL_ERR_INVALID_ARG);

    MU_CHECK(ql_str_create(NULL, &str) == QL_OK);
    MU_CHECK(str != NULL);
    ql_str_destroy(str);

    str = (ql_str_t*)&g_probe;
    MU_CHECK(ql_str_create_from(NULL, NULL, &str) == QL_ERR_INVALID_ARG);
    MU_CHECK(str == NULL);
}

MU_TEST(test_create_from_copies_the_text)
{
    ql_str_t* str = NULL;
    char source[16];

    strcpy(source, "hello");
    MU_ASSERT(ql_str_create_from(NULL, source, &str) == QL_OK);

    MU_CHECK(ql_str_size(str) == 5);
    MU_CHECK(strcmp(ql_str_cstr(str), "hello") == 0);
    MU_CHECK(!ql_str_empty(str));

    /* The copy is independent of the source buffer. */
    source[0] = 'X';
    MU_CHECK(strcmp(ql_str_cstr(str), "hello") == 0);

    ql_str_destroy(str);
}

MU_TEST(test_lifecycle_and_null_tolerance)
{
    ql_str_t* str = NULL;

    MU_ASSERT(ql_str_create(NULL, &str) == QL_OK);
    MU_CHECK(ql_str_size(str) == 0);
    MU_CHECK(ql_str_capacity(str) == 0);
    MU_CHECK(ql_str_empty(str));

    /* An empty string still has to hand out a usable C string. */
    MU_ASSERT(ql_str_cstr(str) != NULL);
    MU_CHECK(strcmp(ql_str_cstr(str), "") == 0);

    ql_str_destroy(str);

    /* Destroying NULL and clearing NULL must both be no-ops. */
    ql_str_destroy(NULL);
    ql_str_clear(NULL);

    MU_CHECK(ql_str_size(NULL) == 0);
    MU_CHECK(ql_str_capacity(NULL) == 0);
    MU_CHECK(ql_str_empty(NULL));
    MU_CHECK(ql_str_cstr(NULL) == NULL);
}

MU_TEST(test_null_string_is_rejected_by_mutators)
{
    size_t index = 0;

    MU_CHECK(ql_str_assign(NULL, "x") == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_assign_n(NULL, "x", 1) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_append(NULL, "x") == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_append_n(NULL, "x", 1) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_append_char(NULL, 'x') == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_append_str(NULL, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_append_printf(NULL, "x") == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_reserve(NULL, 8) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_find(NULL, "x", &index) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_assign_replaces_contents)
{
    ql_str_t* str = make_string("first");

    MU_ASSERT(str != NULL);
    MU_ASSERT(ql_str_assign(str, "second") == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "second") == 0);
    MU_CHECK(ql_str_size(str) == 6);

    /* Shorter than before: the tail must not survive. */
    MU_ASSERT(ql_str_assign(str, "a") == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "a") == 0);
    MU_CHECK(ql_str_size(str) == 1);

    /* Empty assign clears. */
    MU_ASSERT(ql_str_assign(str, "") == QL_OK);
    MU_CHECK(ql_str_size(str) == 0);
    MU_CHECK(strcmp(ql_str_cstr(str), "") == 0);
    MU_CHECK(ql_str_empty(str));

    ql_str_destroy(str);
}

MU_TEST(test_embedded_nul_bytes_are_kept)
{
    ql_str_t* str = make_string(NULL);
    static const char data[] = {'a', 'b', '\0', 'c', 'd'};

    MU_ASSERT(str != NULL);
    MU_ASSERT(ql_str_assign_n(str, data, sizeof data) == QL_OK);

    /* The length is authoritative... */
    MU_CHECK(ql_str_size(str) == 5);
    /* ...and the C string view stops at the embedded NUL. */
    MU_CHECK(strlen(ql_str_cstr(str)) == 2);
    MU_CHECK(strcmp(ql_str_cstr(str), "ab") == 0);

    /* A 5 byte string can never equal the 2 byte C string "ab". */
    MU_CHECK(!ql_str_equal_cstr(str, "ab"));

    /* Appending after an embedded NUL keeps counting from the full length. */
    MU_ASSERT(ql_str_append_char(str, 'e') == QL_OK);
    MU_CHECK(ql_str_size(str) == 6);
    MU_CHECK(ql_str_cstr(str)[5] == 'e');

    ql_str_destroy(str);
}

MU_TEST(test_append_variants)
{
    ql_str_t* str = make_string("a");
    ql_str_t* other = make_string("XYZ");

    MU_ASSERT(str != NULL && other != NULL);

    MU_ASSERT(ql_str_append(str, "b") == QL_OK);
    MU_ASSERT(ql_str_append_char(str, 'c') == QL_OK);
    MU_ASSERT(ql_str_append_n(str, "de", 2) == QL_OK);
    MU_ASSERT(ql_str_append_str(str, other) == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "abcdeXYZ") == 0);

    /* Appending nothing is not an error and changes nothing. */
    MU_ASSERT(ql_str_append_n(str, NULL, 0) == QL_OK);
    MU_ASSERT(ql_str_append(str, "") == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "abcdeXYZ") == 0);

    ql_str_destroy(other);
    ql_str_destroy(str);
}

MU_TEST(test_appending_a_string_to_itself)
{
    ql_str_t* str = make_string("abc");

    MU_ASSERT(str != NULL);

    /* The growth step moves the buffer, so the source pointer has to be
     * re-derived from an offset. This is the case the aliasing guard exists
     * for. */
    MU_ASSERT(ql_str_append_str(str, str) == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "abcabc") == 0);
    MU_CHECK(ql_str_size(str) == 6);

    /* Same thing through the C string view. */
    MU_ASSERT(ql_str_append(str, ql_str_cstr(str)) == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "abcabcabcabc") == 0);

    /* And assigning from inside the buffer must not invalidate the source. */
    MU_ASSERT(ql_str_assign_n(str, ql_str_cstr(str) + 6, 6) == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "abcabc") == 0);

    ql_str_destroy(str);
}

MU_TEST(test_append_printf)
{
    ql_str_t* str = make_string("value=");
    char big[512];

    MU_ASSERT(str != NULL);

    MU_ASSERT(ql_str_append_printf(str, "%d/%s", 42, "ok") == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "value=42/ok") == 0);

    /* A result far beyond the initial retry size has to grow, not truncate. */
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    ql_str_clear(str);
    MU_ASSERT(ql_str_append_printf(str, "%s", big) == QL_OK);
    MU_CHECK(ql_str_size(str) == sizeof big - 1);
    MU_CHECK(strcmp(ql_str_cstr(str), big) == 0);

    /* An empty format appends nothing but still succeeds. */
    ql_str_clear(str);
    MU_ASSERT(ql_str_append_printf(str, "%s", "") == QL_OK);
    MU_CHECK(ql_str_size(str) == 0);
    MU_CHECK(strcmp(ql_str_cstr(str), "") == 0);

    ql_str_destroy(str);
}

MU_TEST(test_reserve_never_shrinks)
{
    ql_str_t* str = make_string("abc");
    size_t before;

    MU_ASSERT(str != NULL);
    MU_ASSERT(ql_str_reserve(str, 64) == QL_OK);
    MU_CHECK(ql_str_capacity(str) >= 64);
    MU_CHECK(strcmp(ql_str_cstr(str), "abc") == 0);

    /* Reserving less than the current capacity changes nothing. */
    before = ql_str_capacity(str);
    MU_ASSERT(ql_str_reserve(str, 1) == QL_OK);
    MU_CHECK(ql_str_capacity(str) == before);
    MU_CHECK(strcmp(ql_str_cstr(str), "abc") == 0);

    ql_str_destroy(str);
}

MU_TEST(test_clear_keeps_the_buffer)
{
    ql_str_t* str = make_string("something reasonably long");
    size_t capacity;

    MU_ASSERT(str != NULL);
    MU_ASSERT(ql_str_append(str, " and longer still") == QL_OK);
    capacity = ql_str_capacity(str);
    MU_CHECK(capacity > 0);

    ql_str_clear(str);
    MU_CHECK(ql_str_size(str) == 0);
    MU_CHECK(ql_str_empty(str));
    MU_CHECK(strcmp(ql_str_cstr(str), "") == 0);
    MU_CHECK(ql_str_capacity(str) == capacity);

    /* Still usable after clearing. */
    MU_ASSERT(ql_str_append(str, "again") == QL_OK);
    MU_CHECK(strcmp(ql_str_cstr(str), "again") == 0);

    ql_str_destroy(str);
}

MU_TEST(test_comparison)
{
    ql_str_t* empty = make_string("");
    ql_str_t* a = make_string("abc");
    ql_str_t* b = make_string("abc");
    ql_str_t* ab = make_string("ab");
    ql_str_t* abd = make_string("abd");

    MU_ASSERT(empty != NULL && a != NULL && b != NULL && ab != NULL && abd != NULL);

    MU_CHECK(ql_str_compare(a, b) == 0);
    MU_CHECK(ql_str_compare(a, a) == 0);
    MU_CHECK(ql_str_compare(ab, a) < 0); /* prefix sorts first */
    MU_CHECK(ql_str_compare(a, ab) > 0);
    MU_CHECK(ql_str_compare(a, abd) < 0);
    MU_CHECK(ql_str_compare(abd, a) > 0);
    MU_CHECK(ql_str_compare(empty, a) < 0);

    /* NULL is treated as empty. */
    MU_CHECK(ql_str_compare(NULL, empty) == 0);
    MU_CHECK(ql_str_compare(NULL, a) < 0);
    MU_CHECK(ql_str_compare(a, NULL) > 0);
    MU_CHECK(ql_str_compare(NULL, NULL) == 0);

    /* Equality is stricter than "same prefix". */
    MU_CHECK(ql_str_equal(a, b));
    MU_CHECK(!ql_str_equal(a, ab));
    MU_CHECK(!ql_str_equal(a, NULL));
    MU_CHECK(!ql_str_equal(NULL, a));
    MU_CHECK(!ql_str_equal(NULL, NULL));

    MU_CHECK(ql_str_equal_cstr(a, "abc"));
    MU_CHECK(!ql_str_equal_cstr(a, "ab"));
    MU_CHECK(!ql_str_equal_cstr(a, "abcd"));
    MU_CHECK(!ql_str_equal_cstr(a, NULL));
    MU_CHECK(!ql_str_equal_cstr(NULL, "abc"));
    MU_CHECK(ql_str_equal_cstr(empty, ""));

    ql_str_destroy(abd);
    ql_str_destroy(ab);
    ql_str_destroy(b);
    ql_str_destroy(a);
    ql_str_destroy(empty);
}

MU_TEST(test_find)
{
    ql_str_t* str = make_string("the quick brown fox");
    size_t index = 999;

    MU_ASSERT(str != NULL);

    MU_CHECK(ql_str_find(str, "the", &index) == QL_OK);
    MU_CHECK(index == 0);

    MU_CHECK(ql_str_find(str, "quick", &index) == QL_OK);
    MU_CHECK(index == 4);

    MU_CHECK(ql_str_find(str, "fox", &index) == QL_OK);
    MU_CHECK(index == 16);

    /* An empty needle matches at the start, as strstr does. */
    MU_CHECK(ql_str_find(str, "", &index) == QL_OK);
    MU_CHECK(index == 0);

    /* Not found leaves the output untouched. */
    index = 999;
    MU_CHECK(ql_str_find(str, "cat", &index) == QL_ERR_NOT_FOUND);
    MU_CHECK(index == 999);

    /* A needle longer than the haystack cannot match. */
    MU_CHECK(ql_str_find(str, "the quick brown fox and more", &index) == QL_ERR_NOT_FOUND);

    /* Bad arguments. */
    MU_CHECK(ql_str_find(str, NULL, &index) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_str_find(str, "x", NULL) == QL_ERR_INVALID_ARG);

    ql_str_destroy(str);
}

MU_TEST(test_no_leaks)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_str_t* str = NULL;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    MU_ASSERT(ql_str_create(&alloc, &str) == QL_OK);
    MU_ASSERT(ql_str_append(str, "hello") == QL_OK);
    MU_ASSERT(ql_str_append_printf(str, "%d", 123) == QL_OK);
    MU_ASSERT(ql_str_reserve(str, 512) == QL_OK);
    ql_str_destroy(str);

    MU_CHECK(state.live_blocks == 0);
    MU_CHECK(state.total_allocs >= 2);
    MU_CHECK(state.total_reallocs >= 1);
}

MU_TEST(test_allocation_failure_keeps_contents)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_str_t* str = NULL;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    MU_ASSERT(ql_str_create(&alloc, &str) == QL_OK);
    MU_ASSERT(ql_str_append(str, "hello") == QL_OK);

    /* The next allocation fails, and the following append needs one. */
    state.fail_at = state.alloc_calls + 1;
    MU_CHECK(ql_str_append(str, " world, long enough to require growth") == QL_ERR_NOMEM);

    /* The string is unchanged and still NUL terminated. */
    MU_CHECK(ql_str_equal_cstr(str, "hello"));
    MU_CHECK(ql_str_size(str) == 5);
    MU_CHECK(ql_str_cstr(str)[5] == '\0');

    state.fail_at = 0;
    MU_ASSERT(ql_str_append(str, " world") == QL_OK);
    MU_CHECK(ql_str_equal_cstr(str, "hello world"));

    ql_str_destroy(str);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_failures_are_raised)
{
    ql_str_t* str = make_string("x");
    size_t index = 0;

    MU_ASSERT(str != NULL);

    probe_begin();
    MU_CHECK(ql_str_append(NULL, "x") == QL_ERR_INVALID_ARG);
    probe_expect(1, QL_ERR_INVALID_ARG);

    MU_CHECK(ql_str_assign(str, NULL) == QL_ERR_INVALID_ARG);
    probe_expect(2, QL_ERR_INVALID_ARG);

    MU_CHECK(ql_str_find(str, "zzz", &index) == QL_ERR_NOT_FOUND);
    /* A miss is not an error worth reporting. */
    probe_expect(2, QL_ERR_INVALID_ARG);

    MU_CHECK(ql_str_create(NULL, NULL) == QL_ERR_INVALID_ARG);
    probe_expect(3, QL_ERR_INVALID_ARG);

    ql_set_error_handler(NULL, NULL);
    ql_str_destroy(str);
}

MU_TEST_SUITE(test_suite_str)
{
    MU_RUN_TEST(test_create_rejects_bad_arguments);
    MU_RUN_TEST(test_create_from_copies_the_text);
    MU_RUN_TEST(test_lifecycle_and_null_tolerance);
    MU_RUN_TEST(test_null_string_is_rejected_by_mutators);
    MU_RUN_TEST(test_assign_replaces_contents);
    MU_RUN_TEST(test_embedded_nul_bytes_are_kept);
    MU_RUN_TEST(test_append_variants);
    MU_RUN_TEST(test_appending_a_string_to_itself);
    MU_RUN_TEST(test_append_printf);
    MU_RUN_TEST(test_reserve_never_shrinks);
    MU_RUN_TEST(test_clear_keeps_the_buffer);
    MU_RUN_TEST(test_comparison);
    MU_RUN_TEST(test_find);
    MU_RUN_TEST(test_no_leaks);
    MU_RUN_TEST(test_allocation_failure_keeps_contents);
    MU_RUN_TEST(test_failures_are_raised);
}
