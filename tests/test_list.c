#include "test_suites.h"

#include "qlib/ql_list.h"

#include <stdio.h>
#include <string.h>

#include "test_alloc.h"

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/** @brief An error-handler probe, as in the other suites. */
typedef struct
{
    int calls;
    ql_ret_t code;
} list_probe;

static list_probe g_probe;

static void list_probe_record(ql_ret_t code, const char* message, void* userdata)
{
    list_probe* probe = userdata;

    (void)message;
    probe->calls++;
    probe->code = code;
}

static void probe_begin(void)
{
    g_probe.calls = 0;
    g_probe.code = QL_OK;
    ql_set_error_handler(list_probe_record, &g_probe);
}

static void probe_expect(int calls)
{
    if (g_probe.calls != calls)
    {
        fprintf(stderr, "  handler called %d time(s), expected %d\n", g_probe.calls, calls);
    }
    MU_CHECK(g_probe.calls == calls);
}

/** @brief Append 0..count-1 and fail the test on any error. */
static void fill_sequence(ql_list_t* list, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        MU_ASSERT(ql_list_push_back(list, &i, NULL) == QL_OK);
    }
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

MU_TEST(test_create_rejects_bad_arguments)
{
    ql_list_t* list = (ql_list_t*)&g_probe;

    MU_CHECK(ql_list_create(NULL, 0, &list) == QL_ERR_INVALID_ARG);
    MU_CHECK(list == NULL);

    MU_CHECK(ql_list_create(NULL, sizeof(int), NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_lifecycle)
{
    ql_list_t* list = NULL;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);
    MU_CHECK(ql_list_size(list) == 0);
    MU_CHECK(ql_list_empty(list));
    MU_CHECK(ql_list_elem_size(list) == sizeof(int));
    MU_CHECK(ql_list_first(list) == NULL);
    MU_CHECK(ql_list_last(list) == NULL);

    ql_list_destroy(list);
    ql_list_destroy(NULL);
    ql_list_clear(NULL);

    MU_CHECK(ql_list_size(NULL) == 0);
    MU_CHECK(ql_list_empty(NULL));
    MU_CHECK(ql_list_elem_size(NULL) == 0);
    MU_CHECK(ql_list_first(NULL) == NULL);
    MU_CHECK(ql_list_last(NULL) == NULL);
    MU_CHECK(ql_list_next(NULL) == NULL);
    MU_CHECK(ql_list_prev(NULL) == NULL);
}

MU_TEST(test_push_and_peek)
{
    ql_list_t* list = NULL;
    int value;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);

    value = 2;
    MU_ASSERT(ql_list_push_back(list, &value, NULL) == QL_OK);
    value = 1;
    MU_ASSERT(ql_list_push_front(list, &value, NULL) == QL_OK);
    value = 3;
    MU_ASSERT(ql_list_push_back(list, &value, NULL) == QL_OK);

    /* Order is now 1, 2, 3. */
    MU_CHECK(ql_list_size(list) == 3);
    MU_CHECK(!ql_list_empty(list));

    MU_ASSERT(ql_list_front(list, &out) == QL_OK);
    MU_CHECK(out == 1);
    MU_ASSERT(ql_list_back(list, &out) == QL_OK);
    MU_CHECK(out == 3);

    /* Peeking does not remove. */
    MU_CHECK(ql_list_size(list) == 3);

    ql_list_destroy(list);
}

MU_TEST(test_pop_from_both_ends)
{
    ql_list_t* list = NULL;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);

    /* Popping an empty list reports it rather than crashing. */
    MU_CHECK(ql_list_pop_front(list, &out) == QL_ERR_NOT_FOUND);
    MU_CHECK(ql_list_pop_back(list, &out) == QL_ERR_NOT_FOUND);
    MU_CHECK(ql_list_front(list, &out) == QL_ERR_NOT_FOUND);
    MU_CHECK(ql_list_back(list, &out) == QL_ERR_NOT_FOUND);

    fill_sequence(list, 4); /* 0, 1, 2, 3 */

    MU_ASSERT(ql_list_pop_front(list, &out) == QL_OK);
    MU_CHECK(out == 0);
    MU_ASSERT(ql_list_pop_back(list, &out) == QL_OK);
    MU_CHECK(out == 3);
    MU_ASSERT(ql_list_pop_front(list, &out) == QL_OK);
    MU_CHECK(out == 1);
    MU_ASSERT(ql_list_pop_back(list, &out) == QL_OK);
    MU_CHECK(out == 2);

    MU_CHECK(ql_list_empty(list));
    MU_CHECK(ql_list_pop_front(list, &out) == QL_ERR_NOT_FOUND);

    /* The list is still usable after emptying. */
    fill_sequence(list, 2);
    MU_CHECK(ql_list_size(list) == 2);

    /* A NULL out_elem discards instead of copying. */
    MU_CHECK(ql_list_pop_front(list, NULL) == QL_OK);
    MU_CHECK(ql_list_size(list) == 1);

    ql_list_destroy(list);
}

MU_TEST(test_iteration_both_directions)
{
    ql_list_t* list = NULL;
    ql_list_node_t* node;
    int expected;
    int seen = 0;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);
    fill_sequence(list, 5); /* 0..4 */

    expected = 0;
    for (node = ql_list_first(list); node != NULL; node = ql_list_next(node))
    {
        MU_ASSERT(ql_list_node_value(list, node, &out) == QL_OK);
        MU_CHECK(out == expected);
        expected++;
        seen++;
    }
    MU_CHECK(seen == 5);

    expected = 4;
    seen = 0;
    for (node = ql_list_last(list); node != NULL; node = ql_list_prev(node))
    {
        MU_ASSERT(ql_list_node_value(list, node, &out) == QL_OK);
        MU_CHECK(out == expected);
        expected--;
        seen++;
    }
    MU_CHECK(seen == 5);

    /* The ends are each other's neighbours. */
    MU_CHECK(ql_list_prev(ql_list_first(list)) == NULL);
    MU_CHECK(ql_list_next(ql_list_last(list)) == NULL);

    ql_list_destroy(list);
}

MU_TEST(test_remove_from_anywhere)
{
    ql_list_t* list = NULL;
    ql_list_node_t* first;
    ql_list_node_t* middle;
    ql_list_node_t* last;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);

    {
        int a = 10;
        int b = 20;
        int c = 30;

        MU_ASSERT(ql_list_push_back(list, &a, &first) == QL_OK);
        MU_ASSERT(ql_list_push_back(list, &b, &middle) == QL_OK);
        MU_ASSERT(ql_list_push_back(list, &c, &last) == QL_OK);
    }

    /* Removing the middle leaves the ends linked to each other. */
    MU_ASSERT(ql_list_remove(list, middle) == QL_OK);
    MU_CHECK(ql_list_size(list) == 2);
    MU_ASSERT(ql_list_node_value(list, first, &out) == QL_OK);
    MU_CHECK(out == 10);
    MU_ASSERT(ql_list_node_value(list, last, &out) == QL_OK);
    MU_CHECK(out == 30);
    MU_CHECK(ql_list_next(first) == last);
    MU_CHECK(ql_list_prev(last) == first);

    /* Removing the head moves the head forward. */
    MU_ASSERT(ql_list_remove(list, first) == QL_OK);
    MU_CHECK(ql_list_size(list) == 1);
    MU_CHECK(ql_list_first(list) == last);
    MU_CHECK(ql_list_last(list) == last);

    /* Removing the tail leaves an empty list with both ends NULL. */
    MU_ASSERT(ql_list_remove(list, last) == QL_OK);
    MU_CHECK(ql_list_empty(list));
    MU_CHECK(ql_list_first(list) == NULL);
    MU_CHECK(ql_list_last(list) == NULL);

    ql_list_destroy(list);
}

MU_TEST(test_nodes_survive_activity_around_them)
{
    ql_list_t* list = NULL;
    ql_list_node_t* pinned = NULL;
    ql_list_node_t* scratch = NULL;
    int value = 42;
    int out = 0;
    int i;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);
    MU_ASSERT(ql_list_push_back(list, &value, &pinned) == QL_OK);

    /* Churn around the pinned node: 50 inserts and removals on both sides. */
    for (i = 0; i < 50; i++)
    {
        int other = i;

        MU_ASSERT(ql_list_push_front(list, &other, NULL) == QL_OK);
        MU_ASSERT(ql_list_push_back(list, &other, &scratch) == QL_OK);
        MU_ASSERT(ql_list_remove(list, scratch) == QL_OK);
    }
    MU_CHECK(ql_list_size(list) == 51);

    /* The handle still reads back the value it was created with. */
    MU_ASSERT(ql_list_node_value(list, pinned, &out) == QL_OK);
    MU_CHECK(out == 42);

    /* And removing it is still O(1) and leaves the rest intact. */
    MU_ASSERT(ql_list_remove(list, pinned) == QL_OK);
    MU_CHECK(ql_list_size(list) == 50);

    ql_list_destroy(list);
}

MU_TEST(test_remove_every_node_through_its_handle)
{
    enum
    {
        COUNT = 100,
    };
    ql_list_t* list = NULL;
    ql_list_node_t* nodes[COUNT];
    int i;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);

    for (i = 0; i < COUNT; i++)
    {
        MU_ASSERT(ql_list_push_back(list, &i, &nodes[i]) == QL_OK);
    }
    MU_CHECK(ql_list_size(list) == COUNT);

    /* Remove every odd one; the even ones must all still be intact. */
    for (i = 1; i < COUNT; i += 2)
    {
        MU_ASSERT(ql_list_remove(list, nodes[i]) == QL_OK);
    }
    MU_CHECK(ql_list_size(list) == COUNT / 2);

    for (i = 0; i < COUNT; i += 2)
    {
        MU_ASSERT(ql_list_node_value(list, nodes[i], &out) == QL_OK);
        MU_CHECK(out == i);
    }

    /* Then the rest. */
    for (i = 0; i < COUNT; i += 2)
    {
        MU_ASSERT(ql_list_remove(list, nodes[i]) == QL_OK);
    }
    MU_CHECK(ql_list_empty(list));
    MU_CHECK(ql_list_first(list) == NULL);

    ql_list_destroy(list);
}

MU_TEST(test_insert_before_and_after)
{
    ql_list_t* list = NULL;
    ql_list_node_t* a = NULL;
    ql_list_node_t* c = NULL;
    ql_list_node_t* mid = NULL;
    ql_list_node_t* node;
    int expected[] = {1, 2, 3, 4, 5};
    int index = 0;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);

    {
        int one = 1;
        int five = 5;

        MU_ASSERT(ql_list_push_back(list, &one, &a) == QL_OK);
        MU_ASSERT(ql_list_push_back(list, &five, &c) == QL_OK);
    }

    /* 1, 5 -> insert 3 before 5 -> 1, 3, 5 */
    {
        int three = 3;

        MU_ASSERT(ql_list_insert_before(list, c, &three, &mid) == QL_OK);
    }
    /* -> insert 2 after 1, and 4 after 3 -> 1, 2, 3, 4, 5 */
    {
        int two = 2;
        int four = 4;

        MU_ASSERT(ql_list_insert_after(list, a, &two, NULL) == QL_OK);
        MU_ASSERT(ql_list_insert_after(list, mid, &four, NULL) == QL_OK);
    }

    MU_CHECK(ql_list_size(list) == 5);
    for (node = ql_list_first(list); node != NULL; node = ql_list_next(node))
    {
        MU_ASSERT(ql_list_node_value(list, node, &out) == QL_OK);
        MU_CHECK(index < 5);
        MU_CHECK(out == expected[index]);
        index++;
    }
    MU_CHECK(index == 5);

    /* Inserting before the head replaces the head. */
    {
        int zero = 0;

        MU_ASSERT(ql_list_insert_before(list, ql_list_first(list), &zero, NULL) == QL_OK);
    }
    MU_ASSERT(ql_list_front(list, &out) == QL_OK);
    MU_CHECK(out == 0);

    /* Inserting after the tail replaces the tail. */
    {
        int six = 6;

        MU_ASSERT(ql_list_insert_after(list, ql_list_last(list), &six, NULL) == QL_OK);
    }
    MU_ASSERT(ql_list_back(list, &out) == QL_OK);
    MU_CHECK(out == 6);
    MU_CHECK(ql_list_size(list) == 7);

    ql_list_destroy(list);
}

MU_TEST(test_clear_and_reuse)
{
    ql_list_t* list = NULL;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);
    fill_sequence(list, 10);
    MU_CHECK(ql_list_size(list) == 10);

    ql_list_clear(list);
    MU_CHECK(ql_list_empty(list));
    MU_CHECK(ql_list_first(list) == NULL);
    MU_CHECK(ql_list_last(list) == NULL);
    MU_CHECK(ql_list_front(list, &out) == QL_ERR_NOT_FOUND);

    fill_sequence(list, 3);
    MU_CHECK(ql_list_size(list) == 3);
    MU_ASSERT(ql_list_front(list, &out) == QL_OK);
    MU_CHECK(out == 0);

    ql_list_destroy(list);
}

MU_TEST(test_large_list_integrity)
{
    enum
    {
        COUNT = 1000,
    };
    ql_list_t* list = NULL;
    ql_list_node_t* node;
    int i = 0;
    int out = 0;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);
    fill_sequence(list, COUNT);
    MU_CHECK(ql_list_size(list) == COUNT);

    for (node = ql_list_first(list); node != NULL; node = ql_list_next(node))
    {
        MU_ASSERT(ql_list_node_value(list, node, &out) == QL_OK);
        MU_CHECK(out == i);
        i++;
    }
    MU_CHECK(i == COUNT);

    /* Then drain from the back, checking the order reverses. */
    for (i = COUNT - 1; i >= 0; i--)
    {
        MU_ASSERT(ql_list_pop_back(list, &out) == QL_OK);
        MU_CHECK(out == i);
    }
    MU_CHECK(ql_list_empty(list));

    ql_list_destroy(list);
}

MU_TEST(test_null_arguments_are_rejected)
{
    ql_list_t* list = NULL;
    int value = 1;
    int out = 0;
    ql_list_node_t* node = NULL;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);

    MU_CHECK(ql_list_push_front(NULL, &value, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_push_back(NULL, &value, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_push_front(list, NULL, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_push_back(list, NULL, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_insert_before(NULL, NULL, &value, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_insert_after(NULL, NULL, &value, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_remove(NULL, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_pop_front(NULL, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_pop_back(NULL, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_front(NULL, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_front(list, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_back(NULL, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_back(list, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_node_value(NULL, NULL, &out) == QL_ERR_INVALID_ARG);

    MU_ASSERT(ql_list_push_back(list, &value, &node) == QL_OK);
    MU_CHECK(ql_list_node_value(list, node, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_node_value(list, NULL, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_list_remove(list, node) == QL_OK);

    ql_list_destroy(list);
}

MU_TEST(test_no_leaks)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_list_t* list = NULL;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    MU_ASSERT(ql_list_create(&alloc, sizeof(int), &list) == QL_OK);
    fill_sequence(list, 100);
    /* One block for the list plus one per node. */
    MU_CHECK(state.live_blocks == 101);

    /* Removing a node frees exactly that block. */
    MU_ASSERT(ql_list_remove(list, ql_list_first(list)) == QL_OK);
    MU_CHECK(state.live_blocks == 100);

    ql_list_destroy(list);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_allocation_failure_keeps_the_list_intact)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_list_t* list = NULL;
    int value = 1;
    int out = 0;
    size_t before;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    MU_ASSERT(ql_list_create(&alloc, sizeof(int), &list) == QL_OK);
    value = 1;
    MU_ASSERT(ql_list_push_back(list, &value, NULL) == QL_OK);
    value = 2;
    MU_ASSERT(ql_list_push_back(list, &value, NULL) == QL_OK);
    before = ql_list_size(list);

    /* The next node allocation fails. */
    state.fail_at = state.alloc_calls + 1;
    value = 3;
    MU_CHECK(ql_list_push_back(list, &value, NULL) == QL_ERR_NOMEM);
    MU_CHECK(ql_list_size(list) == before);
    MU_CHECK(state.live_blocks == 3);

    /* The list still reads correctly. */
    MU_ASSERT(ql_list_back(list, &out) == QL_OK);
    MU_CHECK(out == 2);

    state.fail_at = 0;
    MU_ASSERT(ql_list_push_back(list, &value, NULL) == QL_OK);
    MU_CHECK(ql_list_size(list) == 3);

    ql_list_destroy(list);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_failures_are_raised)
{
    ql_list_t* list = NULL;

    MU_ASSERT(ql_list_create(NULL, sizeof(int), &list) == QL_OK);

    probe_begin();
    MU_CHECK(ql_list_push_back(NULL, NULL, NULL) == QL_ERR_INVALID_ARG);
    probe_expect(1);

    /* Popping an empty list is not worth reporting. */
    MU_CHECK(ql_list_pop_front(list, NULL) == QL_ERR_NOT_FOUND);
    probe_expect(1);

    {
        /* A failed create sets *out to NULL; a separate out parameter keeps
         * the live list reachable. */
        ql_list_t* rejected = (ql_list_t*)&g_probe;

        MU_CHECK(ql_list_create(NULL, 0, &rejected) == QL_ERR_INVALID_ARG);
        MU_CHECK(rejected == NULL);
    }
    probe_expect(2);

    ql_set_error_handler(NULL, NULL);
    ql_list_destroy(list);
}

MU_TEST_SUITE(test_suite_list)
{
    MU_RUN_TEST(test_create_rejects_bad_arguments);
    MU_RUN_TEST(test_lifecycle);
    MU_RUN_TEST(test_push_and_peek);
    MU_RUN_TEST(test_pop_from_both_ends);
    MU_RUN_TEST(test_iteration_both_directions);
    MU_RUN_TEST(test_remove_from_anywhere);
    MU_RUN_TEST(test_nodes_survive_activity_around_them);
    MU_RUN_TEST(test_remove_every_node_through_its_handle);
    MU_RUN_TEST(test_insert_before_and_after);
    MU_RUN_TEST(test_clear_and_reuse);
    MU_RUN_TEST(test_large_list_integrity);
    MU_RUN_TEST(test_null_arguments_are_rejected);
    MU_RUN_TEST(test_no_leaks);
    MU_RUN_TEST(test_allocation_failure_keeps_the_list_intact);
    MU_RUN_TEST(test_failures_are_raised);
}
