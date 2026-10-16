#include "test_suites.h"

#include "qlib/ql_vector.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * A tracking allocator whose realloc ALWAYS returns a different block.
 *
 * That matters: with libc realloc a growing allocation may happen to stay put,
 * which would hide a use-after-free when the caller passes a pointer into the
 * vector's own buffer. Forcing a move makes the aliasing tests deterministic.
 * It also counts live blocks so leaks are visible.
 * ------------------------------------------------------------------------ */

typedef struct
{
    size_t size;
} block_header;

typedef struct
{
    size_t live_blocks;
    size_t total_allocs;
    size_t total_reallocs;
} tracking_state;

static int g_sentinel;

static void* tracking_malloc(size_t size, void* userdata)
{
    tracking_state* state = userdata;
    block_header* header = malloc(sizeof *header + size);

    if (header == NULL)
    {
        return NULL;
    }
    header->size = size;
    state->live_blocks++;
    state->total_allocs++;
    return header + 1;
}

static void* tracking_realloc(void* ptr, size_t size, void* userdata)
{
    tracking_state* state = userdata;
    block_header* header;
    void* fresh;
    size_t copy;

    if (ptr == NULL)
    {
        return tracking_malloc(size, userdata);
    }

    header = (block_header*)ptr - 1;
    copy = header->size < size ? header->size : size;

    /* Deliberately malloc+copy+free instead of realloc, so the block moves. */
    fresh = tracking_malloc(size, userdata);
    if (fresh == NULL)
    {
        return NULL;
    }
    memcpy(fresh, ptr, copy);

    free(header);
    state->live_blocks--;
    state->total_reallocs++;
    return fresh;
}

static void tracking_free(void* ptr, void* userdata)
{
    tracking_state* state = userdata;

    if (ptr == NULL)
    {
        return;
    }
    free((block_header*)ptr - 1);
    state->live_blocks--;
}

static ql_allocator_t make_tracking_allocator(tracking_state* state)
{
    ql_allocator_t alloc = ql_default_allocator();

    alloc.malloc = tracking_malloc;
    alloc.realloc = tracking_realloc;
    alloc.free = tracking_free;
    alloc.userdata = state;
    return alloc;
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

MU_TEST(test_create_rejects_bad_arguments)
{
    ql_vector_t* vec = (ql_vector_t*)&g_sentinel;

    MU_CHECK(ql_vector_create(NULL, 0, &vec) == QL_ERR_INVALID_ARG);
    MU_CHECK(vec == NULL);

    MU_CHECK(ql_vector_create(NULL, sizeof(int), NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_lifecycle)
{
    ql_vector_t* vec = NULL;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);
    MU_CHECK(vec != NULL);
    MU_CHECK(ql_vector_size(vec) == 0);
    MU_CHECK(ql_vector_capacity(vec) == 0);
    MU_CHECK(ql_vector_elem_size(vec) == sizeof(int));
    MU_CHECK(ql_vector_empty(vec));
    MU_CHECK(ql_vector_data(vec) == NULL);
    MU_CHECK(ql_vector_at(vec, 0) == NULL);

    ql_vector_destroy(vec);

    /* Destroying NULL must be a no-op. */
    ql_vector_destroy(NULL);
}

MU_TEST(test_queries_tolerate_null)
{
    MU_CHECK(ql_vector_size(NULL) == 0);
    MU_CHECK(ql_vector_capacity(NULL) == 0);
    MU_CHECK(ql_vector_elem_size(NULL) == 0);
    MU_CHECK(ql_vector_empty(NULL));
    MU_CHECK(ql_vector_data(NULL) == NULL);
    MU_CHECK(ql_vector_at(NULL, 0) == NULL);
}

MU_TEST(test_null_vector_is_rejected_by_mutators)
{
    int value = 7;
    int out = 0;

    MU_CHECK(ql_vector_reserve(NULL, 4) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_vector_resize(NULL, 4) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_vector_push_back(NULL, &value) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_vector_pop_back(NULL, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_vector_insert(NULL, 0, &value) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_vector_remove(NULL, 0) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_vector_assign(NULL, 0, &value) == QL_ERR_INVALID_ARG);
    ql_vector_clear(NULL); /* must not crash */
}

MU_TEST(test_push_back_grows_and_preserves_order)
{
    ql_vector_t* vec = NULL;
    int i;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);

    for (i = 0; i < 100; i++)
    {
        int value = i * 7;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
        MU_CHECK(ql_vector_size(vec) == (size_t)(i + 1));
        MU_CHECK(ql_vector_capacity(vec) >= ql_vector_size(vec));
    }

    MU_CHECK(!ql_vector_empty(vec));
    for (i = 0; i < 100; i++)
    {
        int* slot = ql_vector_at(vec, (size_t)i);
        MU_ASSERT(slot != NULL);
        MU_CHECK(*slot == i * 7);
    }
    MU_CHECK(ql_vector_at(vec, 100) == NULL);

    ql_vector_destroy(vec);
}

MU_TEST(test_push_back_rejects_null_element)
{
    ql_vector_t* vec = NULL;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);
    MU_CHECK(ql_vector_push_back(vec, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_vector_size(vec) == 0);
    ql_vector_destroy(vec);
}

MU_TEST(test_pop_back_is_lifo)
{
    ql_vector_t* vec = NULL;
    int i;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);

    for (i = 0; i < 5; i++)
    {
        int value = i;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    }

    for (i = 4; i >= 0; i--)
    {
        int out = -1;
        MU_ASSERT(ql_vector_pop_back(vec, &out) == QL_OK);
        MU_CHECK(out == i);
    }

    MU_CHECK(ql_vector_empty(vec));
    MU_CHECK(ql_vector_pop_back(vec, NULL) == QL_ERR_EMPTY);
    ql_vector_destroy(vec);
}

MU_TEST(test_pop_back_can_discard)
{
    ql_vector_t* vec = NULL;
    int value = 42;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);
    MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);

    /* out == NULL must drop the element without dereferencing anything. */
    MU_CHECK(ql_vector_pop_back(vec, NULL) == QL_OK);
    MU_CHECK(ql_vector_size(vec) == 0);
    ql_vector_destroy(vec);
}

MU_TEST(test_insert_at_begin_middle_and_end)
{
    ql_vector_t* vec = NULL;
    int i;
    static const int expected[] = {99, 0, 1, 50, 2, 3, 4, 77};

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);

    for (i = 0; i < 5; i++)
    {
        int value = i;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    }
    /* {0,1,2,3,4} */
    {
        int value = 99;
        MU_ASSERT(ql_vector_insert(vec, 0, &value) == QL_OK);
    }
    /* {99,0,1,2,3,4} */
    {
        int value = 50;
        MU_ASSERT(ql_vector_insert(vec, 3, &value) == QL_OK);
    }
    /* {99,0,1,50,2,3,4} */
    {
        int value = 77;
        MU_ASSERT(ql_vector_insert(vec, ql_vector_size(vec), &value) == QL_OK);
    }
    /* {99,0,1,50,2,3,4,77} */

    MU_CHECK(ql_vector_size(vec) == sizeof expected / sizeof expected[0]);
    for (i = 0; i < (int)(sizeof expected / sizeof expected[0]); i++)
    {
        int* slot = ql_vector_at(vec, (size_t)i);
        MU_ASSERT(slot != NULL);
        MU_CHECK(*slot == expected[i]);
    }

    ql_vector_destroy(vec);
}

MU_TEST(test_insert_out_of_range)
{
    ql_vector_t* vec = NULL;
    int value = 1;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);
    MU_CHECK(ql_vector_insert(vec, 1, &value) == QL_ERR_OUT_OF_RANGE);
    MU_CHECK(ql_vector_size(vec) == 0);
    MU_CHECK(ql_vector_insert(vec, 0, NULL) == QL_ERR_INVALID_ARG);
    ql_vector_destroy(vec);
}

MU_TEST(test_remove_shifts_down)
{
    ql_vector_t* vec = NULL;
    int i;
    static const int expected[] = {1, 3};

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);
    for (i = 0; i < 4; i++)
    {
        int value = i;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    }
    /* {0,1,2,3} */
    MU_ASSERT(ql_vector_remove(vec, 0) == QL_OK); /* {1,2,3} */
    MU_ASSERT(ql_vector_remove(vec, 1) == QL_OK); /* {1,3} */

    MU_CHECK(ql_vector_size(vec) == 2);
    for (i = 0; i < 2; i++)
    {
        int* slot = ql_vector_at(vec, (size_t)i);
        MU_ASSERT(slot != NULL);
        MU_CHECK(*slot == expected[i]);
    }

    /* Removing the last element is the boundary case. */
    MU_ASSERT(ql_vector_remove(vec, 1) == QL_OK);
    MU_CHECK(ql_vector_size(vec) == 1);
    MU_CHECK(ql_vector_remove(vec, 1) == QL_ERR_OUT_OF_RANGE);
    MU_ASSERT(ql_vector_remove(vec, 0) == QL_OK);
    MU_CHECK(ql_vector_empty(vec));
    MU_CHECK(ql_vector_remove(vec, 0) == QL_ERR_OUT_OF_RANGE);

    ql_vector_destroy(vec);
}

MU_TEST(test_reserve_never_shrinks)
{
    ql_vector_t* vec = NULL;
    size_t capacity;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);

    MU_ASSERT(ql_vector_reserve(vec, 64) == QL_OK);
    capacity = ql_vector_capacity(vec);
    MU_CHECK(capacity >= 64);
    MU_CHECK(ql_vector_size(vec) == 0);

    /* A smaller request must be a no-op, not a shrink. */
    MU_ASSERT(ql_vector_reserve(vec, 4) == QL_OK);
    MU_CHECK(ql_vector_capacity(vec) == capacity);

    ql_vector_destroy(vec);
}

MU_TEST(test_resize_grows_zero_filled_and_shrinks)
{
    ql_vector_t* vec = NULL;
    int i;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);

    MU_ASSERT(ql_vector_resize(vec, 4) == QL_OK);
    MU_CHECK(ql_vector_size(vec) == 4);
    for (i = 0; i < 4; i++)
    {
        int* slot = ql_vector_at(vec, (size_t)i);
        MU_ASSERT(slot != NULL);
        MU_CHECK(*slot == 0); /* newly exposed elements are zero filled */
    }

    /* Write something, grow again, and check the new tail is still zero. */
    *(int*)ql_vector_at(vec, 3) = 1234;
    MU_ASSERT(ql_vector_resize(vec, 8) == QL_OK);
    MU_CHECK(*(int*)ql_vector_at(vec, 3) == 1234);
    for (i = 4; i < 8; i++)
    {
        MU_CHECK(*(int*)ql_vector_at(vec, (size_t)i) == 0);
    }

    MU_ASSERT(ql_vector_resize(vec, 2) == QL_OK);
    MU_CHECK(ql_vector_size(vec) == 2);
    MU_CHECK(*(int*)ql_vector_at(vec, 1) == 0);

    MU_ASSERT(ql_vector_resize(vec, 0) == QL_OK);
    MU_CHECK(ql_vector_empty(vec));

    ql_vector_destroy(vec);
}

MU_TEST(test_assign_and_its_bounds)
{
    ql_vector_t* vec = NULL;
    int value = 5;
    int replacement = 9;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);
    MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);

    MU_ASSERT(ql_vector_assign(vec, 0, &replacement) == QL_OK);
    MU_CHECK(*(int*)ql_vector_at(vec, 0) == 9);
    MU_CHECK(ql_vector_assign(vec, 1, &replacement) == QL_ERR_OUT_OF_RANGE);
    MU_CHECK(ql_vector_assign(vec, 0, NULL) == QL_ERR_INVALID_ARG);

    ql_vector_destroy(vec);
}

MU_TEST(test_clear_keeps_capacity)
{
    ql_vector_t* vec = NULL;
    int value = 1;
    size_t capacity;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);
    MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    capacity = ql_vector_capacity(vec);

    ql_vector_clear(vec);
    MU_CHECK(ql_vector_empty(vec));
    MU_CHECK(ql_vector_capacity(vec) == capacity);

    ql_vector_destroy(vec);
}

/* ---------------------------------------------------------------------------
 * Aliasing: the element argument may point into the vector's own buffer.
 *
 * These run on the always-moving allocator, so a naive implementation that
 * reads the source pointer after growing is guaranteed to touch freed memory.
 * ------------------------------------------------------------------------ */

MU_TEST(test_push_back_from_own_buffer)
{
    tracking_state state = {0, 0, 0};
    ql_allocator_t alloc = make_tracking_allocator(&state);
    ql_vector_t* vec = NULL;
    int i;
    int* last;

    MU_ASSERT(ql_vector_create(&alloc, sizeof(int), &vec) == QL_OK);

    /* Fill exactly to capacity so the next push has to grow (and move). */
    MU_ASSERT(ql_vector_reserve(vec, 8) == QL_OK);
    for (i = 0; i < 8; i++)
    {
        int value = i * 10;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    }
    MU_CHECK(ql_vector_size(vec) == ql_vector_capacity(vec));

    MU_ASSERT(ql_vector_push_back(vec, ql_vector_at(vec, 0)) == QL_OK);

    MU_CHECK(ql_vector_size(vec) == 9);
    last = ql_vector_at(vec, 8);
    MU_ASSERT(last != NULL);
    MU_CHECK(*last == 0);                       /* element 0 was 0*10 */
    MU_CHECK(*(int*)ql_vector_at(vec, 0) == 0); /* and it is still there */
    MU_CHECK(*(int*)ql_vector_at(vec, 1) == 10);

    ql_vector_destroy(vec);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_insert_from_own_buffer)
{
    tracking_state state = {0, 0, 0};
    ql_allocator_t alloc = make_tracking_allocator(&state);
    ql_vector_t* vec = NULL;
    int i;

    MU_ASSERT(ql_vector_create(&alloc, sizeof(int), &vec) == QL_OK);
    MU_ASSERT(ql_vector_reserve(vec, 4) == QL_OK);
    for (i = 0; i < 4; i++)
    {
        int value = (i + 1) * 100;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    }
    /* {100,200,300,400}, size == capacity */

    /* Copy element 3 (400) to the front. Growing and shifting both happen. */
    MU_ASSERT(ql_vector_insert(vec, 0, ql_vector_at(vec, 3)) == QL_OK);

    MU_CHECK(ql_vector_size(vec) == 5);
    MU_CHECK(*(int*)ql_vector_at(vec, 0) == 400);
    MU_CHECK(*(int*)ql_vector_at(vec, 1) == 100);
    MU_CHECK(*(int*)ql_vector_at(vec, 4) == 400);

    ql_vector_destroy(vec);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_insert_from_own_buffer_without_growth)
{
    tracking_state state = {0, 0, 0};
    ql_allocator_t alloc = make_tracking_allocator(&state);
    ql_vector_t* vec = NULL;
    int i;

    MU_ASSERT(ql_vector_create(&alloc, sizeof(int), &vec) == QL_OK);
    MU_ASSERT(ql_vector_reserve(vec, 16) == QL_OK); /* room: no realloc will happen */
    for (i = 0; i < 4; i++)
    {
        int value = (i + 1) * 100;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    }

    /* Only memmove can corrupt this one, there is no realloc involved. */
    MU_ASSERT(ql_vector_insert(vec, 2, ql_vector_at(vec, 0)) == QL_OK);

    MU_CHECK(ql_vector_size(vec) == 5);
    MU_CHECK(*(int*)ql_vector_at(vec, 0) == 100);
    MU_CHECK(*(int*)ql_vector_at(vec, 1) == 200);
    MU_CHECK(*(int*)ql_vector_at(vec, 2) == 100); /* the pinned copy */
    MU_CHECK(*(int*)ql_vector_at(vec, 3) == 300);
    MU_CHECK(*(int*)ql_vector_at(vec, 4) == 400);

    ql_vector_destroy(vec);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_assign_from_own_buffer)
{
    tracking_state state = {0, 0, 0};
    ql_allocator_t alloc = make_tracking_allocator(&state);
    ql_vector_t* vec = NULL;
    int values[3] = {10, 20, 30};
    int i;

    MU_ASSERT(ql_vector_create(&alloc, sizeof(int), &vec) == QL_OK);
    for (i = 0; i < 3; i++)
    {
        MU_ASSERT(ql_vector_push_back(vec, &values[i]) == QL_OK);
    }

    /* Overlapping regions inside the buffer: memmove semantics required. */
    MU_ASSERT(ql_vector_assign(vec, 0, ql_vector_at(vec, 2)) == QL_OK);
    MU_CHECK(*(int*)ql_vector_at(vec, 0) == 30);
    MU_CHECK(*(int*)ql_vector_at(vec, 2) == 30);

    ql_vector_destroy(vec);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_tracking_allocator_balances_on_normal_use)
{
    tracking_state state = {0, 0, 0};
    ql_allocator_t alloc = make_tracking_allocator(&state);
    ql_vector_t* vec = NULL;
    int i;

    MU_ASSERT(ql_vector_create(&alloc, sizeof(int), &vec) == QL_OK);
    for (i = 0; i < 40; i++)
    {
        int value = i;
        MU_ASSERT(ql_vector_push_back(vec, &value) == QL_OK);
    }
    MU_CHECK(state.live_blocks > 0);
    MU_CHECK(state.total_reallocs > 0);

    /* The allocator really is being used, and clearing must not free storage. */
    ql_vector_clear(vec);
    MU_CHECK(state.live_blocks > 0);

    ql_vector_destroy(vec);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_non_scalar_element_type)
{
    typedef struct
    {
        char tag[8];
        int value;
    } record;

    ql_vector_t* vec = NULL;
    record first;
    record second;
    record* slot;

    MU_ASSERT(ql_vector_create(NULL, sizeof(record), &vec) == QL_OK);
    MU_CHECK(ql_vector_elem_size(vec) == sizeof(record));

    memset(&first, 0, sizeof first);
    memcpy(first.tag, "alpha", 6);
    first.value = 1;

    memset(&second, 0, sizeof second);
    memcpy(second.tag, "beta", 5);
    second.value = 2;

    MU_ASSERT(ql_vector_push_back(vec, &first) == QL_OK);
    MU_ASSERT(ql_vector_push_back(vec, &second) == QL_OK);

    slot = ql_vector_at(vec, 0);
    MU_ASSERT(slot != NULL);
    MU_CHECK(strcmp(slot->tag, "alpha") == 0);
    MU_CHECK(slot->value == 1);

    slot = ql_vector_at(vec, 1);
    MU_ASSERT(slot != NULL);
    MU_CHECK(strcmp(slot->tag, "beta") == 0);
    MU_CHECK(slot->value == 2);

    ql_vector_destroy(vec);
}

MU_TEST(test_reserve_overflow_is_rejected)
{
    ql_vector_t* vec = NULL;
    size_t capacity;

    MU_ASSERT(ql_vector_create(NULL, sizeof(int), &vec) == QL_OK);

    MU_CHECK(ql_vector_reserve(vec, SIZE_MAX / sizeof(int) + 1) == QL_ERR_OVERFLOW);
    MU_CHECK(ql_vector_size(vec) == 0);
    MU_CHECK(ql_vector_capacity(vec) == 0);

    /* The failure must leave a usable vector behind. */
    MU_ASSERT(ql_vector_reserve(vec, 4) == QL_OK);
    capacity = ql_vector_capacity(vec);
    MU_CHECK(capacity >= 4);

    ql_vector_destroy(vec);
}

MU_TEST_SUITE(test_suite_vector)
{
    MU_RUN_TEST(test_create_rejects_bad_arguments);
    MU_RUN_TEST(test_lifecycle);
    MU_RUN_TEST(test_queries_tolerate_null);
    MU_RUN_TEST(test_null_vector_is_rejected_by_mutators);
    MU_RUN_TEST(test_push_back_grows_and_preserves_order);
    MU_RUN_TEST(test_push_back_rejects_null_element);
    MU_RUN_TEST(test_pop_back_is_lifo);
    MU_RUN_TEST(test_pop_back_can_discard);
    MU_RUN_TEST(test_insert_at_begin_middle_and_end);
    MU_RUN_TEST(test_insert_out_of_range);
    MU_RUN_TEST(test_remove_shifts_down);
    MU_RUN_TEST(test_reserve_never_shrinks);
    MU_RUN_TEST(test_resize_grows_zero_filled_and_shrinks);
    MU_RUN_TEST(test_assign_and_its_bounds);
    MU_RUN_TEST(test_clear_keeps_capacity);
    MU_RUN_TEST(test_push_back_from_own_buffer);
    MU_RUN_TEST(test_insert_from_own_buffer);
    MU_RUN_TEST(test_insert_from_own_buffer_without_growth);
    MU_RUN_TEST(test_assign_from_own_buffer);
    MU_RUN_TEST(test_tracking_allocator_balances_on_normal_use);
    MU_RUN_TEST(test_non_scalar_element_type);
    MU_RUN_TEST(test_reserve_overflow_is_rejected);
}
