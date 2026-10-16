#include "test_suites.h"

#include "qlib/ql_mem.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Allocators with observable behaviour
 * ------------------------------------------------------------------------ */

typedef struct
{
    size_t mallocs;
    size_t reallocs;
    size_t frees;
} counting_state;

static void* counting_malloc(size_t size, void* userdata)
{
    counting_state* state = userdata;
    state->mallocs++;
    return malloc(size);
}

static void* counting_realloc(void* ptr, size_t size, void* userdata)
{
    counting_state* state = userdata;
    state->reallocs++;
    return realloc(ptr, size);
}

static void counting_free(void* ptr, void* userdata)
{
    counting_state* state = userdata;
    state->frees++;
    free(ptr);
}

/* Always fails, so failure paths are deterministic rather than size dependent. */
static void* failing_malloc(size_t size, void* userdata)
{
    (void)size;
    (void)userdata;
    return NULL;
}

static void* failing_realloc(void* ptr, size_t size, void* userdata)
{
    (void)ptr;
    (void)size;
    (void)userdata;
    return NULL;
}

static void failing_free(void* ptr, void* userdata)
{
    (void)ptr;
    (void)userdata;
}

/* ---------------------------------------------------------------------------
 * Error handler probe
 * ------------------------------------------------------------------------ */

typedef struct
{
    int calls;
    ql_ret_t code;
    const char* message;
    void* userdata;
} handler_state;

static void record_handler(ql_ret_t code, const char* message, void* userdata)
{
    handler_state* state = userdata;
    state->calls++;
    state->code = code;
    state->message = message;
    state->userdata = userdata;
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

MU_TEST(test_default_allocator_is_complete)
{
    ql_allocator_t alloc = ql_default_allocator();
    size_t i;

    MU_CHECK(alloc.malloc != NULL);
    MU_CHECK(alloc.realloc != NULL);
    MU_CHECK(alloc.free != NULL);
    MU_CHECK(alloc.userdata == NULL);
    for (i = 0; i < sizeof alloc.reserved / sizeof alloc.reserved[0]; i++)
    {
        MU_CHECK(alloc.reserved[i] == NULL);
    }
}

MU_TEST(test_alloc_and_dealloc)
{
    unsigned char* p = ql_alloc(NULL, 64);

    MU_ASSERT(p != NULL);
    p[0] = 0xAB;
    p[63] = 0xCD;
    MU_CHECK(p[0] == 0xAB);
    MU_CHECK(p[63] == 0xCD);
    ql_dealloc(NULL, p);

    /* A zero sized request yields NULL and is not an error. */
    MU_CHECK(ql_alloc(NULL, 0) == NULL);

    /* Releasing NULL must be a no-op. */
    ql_dealloc(NULL, NULL);
}

MU_TEST(test_calloc_zero_fills)
{
    unsigned char* p = ql_calloc(NULL, 16, 4);
    size_t i;

    MU_ASSERT(p != NULL);
    for (i = 0; i < 64; i++)
    {
        MU_CHECK(p[i] == 0);
    }

    /* Written bytes must survive; guards against a calloc that forgets memset. */
    p[10] = 0xFF;
    MU_CHECK(p[10] == 0xFF);
    ql_dealloc(NULL, p);

    MU_CHECK(ql_calloc(NULL, 0, 8) == NULL);
}

MU_TEST(test_realloc_preserves_contents)
{
    unsigned char* p = ql_alloc(NULL, 16);
    unsigned char* grown;
    size_t i;

    MU_ASSERT(p != NULL);
    for (i = 0; i < 16; i++)
    {
        p[i] = (unsigned char)i;
    }

    grown = ql_realloc(NULL, p, 256);
    if (grown == NULL)
    {
        ql_dealloc(NULL, p);
    }
    MU_ASSERT(grown != NULL);

    for (i = 0; i < 16; i++)
    {
        MU_CHECK(grown[i] == (unsigned char)i);
    }
    ql_dealloc(NULL, grown);
}

MU_TEST(test_realloc_zero_frees)
{
    counting_state state = {0, 0, 0};
    ql_allocator_t alloc = ql_default_allocator();
    void* p;

    alloc.malloc = counting_malloc;
    alloc.realloc = counting_realloc;
    alloc.free = counting_free;
    alloc.userdata = &state;

    p = ql_alloc(&alloc, 32);
    MU_ASSERT(p != NULL);
    MU_CHECK(state.mallocs == 1);

    MU_CHECK(ql_realloc(&alloc, p, 0) == NULL);
    MU_CHECK(state.frees == 1);

    /* dealloc(NULL) must not reach the allocator. */
    ql_dealloc(&alloc, NULL);
    MU_CHECK(state.frees == 1);
}

MU_TEST(test_custom_allocator_is_actually_used)
{
    counting_state state = {0, 0, 0};
    ql_allocator_t alloc = ql_default_allocator();
    void* p;
    void* grown;

    alloc.malloc = counting_malloc;
    alloc.realloc = counting_realloc;
    alloc.free = counting_free;
    alloc.userdata = &state;

    p = ql_alloc(&alloc, 32);
    MU_ASSERT(p != NULL);

    grown = ql_realloc(&alloc, p, 64);
    if (grown == NULL)
    {
        ql_dealloc(&alloc, p);
    }
    MU_ASSERT(grown != NULL);
    ql_dealloc(&alloc, grown);

    MU_CHECK(state.mallocs == 1);
    MU_CHECK(state.reallocs == 1);
    MU_CHECK(state.frees == 1);
}

MU_TEST(test_size_mul_overflow)
{
    size_t out = 123;

    MU_CHECK(ql_size_mul_overflow(4, 8, &out) == QL_OK);
    MU_CHECK(out == 32);

    /* A zero count can never overflow. */
    MU_CHECK(ql_size_mul_overflow(0, SIZE_MAX, &out) == QL_OK);
    MU_CHECK(out == 0);

    MU_CHECK(ql_size_mul_overflow(SIZE_MAX, 1, &out) == QL_OK);
    MU_CHECK(out == SIZE_MAX);

    /* On failure the result must be left untouched. */
    out = 999;
    MU_CHECK(ql_size_mul_overflow(SIZE_MAX, 2, &out) == QL_ERR_OVERFLOW);
    MU_CHECK(out == 999);
    MU_CHECK(ql_size_mul_overflow(2, SIZE_MAX, &out) == QL_ERR_OVERFLOW);
    MU_CHECK(ql_size_mul_overflow(SIZE_MAX / 2 + 1, 2, &out) == QL_ERR_OVERFLOW);

    MU_CHECK(ql_size_mul_overflow(1, 1, NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_alloc_array_happy_path)
{
    int* values = ql_alloc_array(NULL, 8, sizeof(int));
    size_t i;

    MU_ASSERT(values != NULL);
    for (i = 0; i < 8; i++)
    {
        values[i] = (int)i * 3;
    }
    for (i = 0; i < 8; i++)
    {
        MU_CHECK(values[i] == (int)i * 3);
    }

    values = ql_realloc_array(NULL, values, 32, sizeof(int));
    MU_ASSERT(values != NULL);
    MU_CHECK(values[7] == 21);
    ql_dealloc(NULL, values);
}

MU_TEST(test_array_helpers_reject_overflow)
{
    handler_state state = {0, QL_OK, NULL, NULL};

    ql_set_error_handler(record_handler, &state);

    MU_CHECK(ql_alloc_array(NULL, SIZE_MAX, 2) == NULL);
    MU_CHECK(state.calls == 1);
    MU_CHECK(state.code == QL_ERR_OVERFLOW);

    MU_CHECK(ql_realloc_array(NULL, NULL, SIZE_MAX, 2) == NULL);
    MU_CHECK(state.calls == 2);
    MU_CHECK(state.code == QL_ERR_OVERFLOW);

    MU_CHECK(ql_calloc(NULL, SIZE_MAX, 2) == NULL);
    MU_CHECK(state.calls == 3);
    MU_CHECK(state.code == QL_ERR_OVERFLOW);

    ql_set_error_handler(NULL, NULL);
}

MU_TEST(test_failure_raises_nomem)
{
    ql_allocator_t alloc = ql_default_allocator();
    handler_state state = {0, QL_OK, NULL, NULL};

    alloc.malloc = failing_malloc;
    alloc.realloc = failing_realloc;
    alloc.free = failing_free;

    ql_set_error_handler(record_handler, &state);

    MU_CHECK(ql_alloc(&alloc, 16) == NULL);
    MU_CHECK(state.calls == 1);
    MU_CHECK(state.code == QL_ERR_NOMEM);
    /* MU_ASSERT, not MU_CHECK: the next line dereferences the pointer. */
    MU_ASSERT(state.message != NULL);
    MU_CHECK(strlen(state.message) > 0);
    MU_CHECK(state.userdata == &state);

    MU_CHECK(ql_calloc(&alloc, 4, 4) == NULL);
    MU_CHECK(state.calls == 2);
    MU_CHECK(state.code == QL_ERR_NOMEM);

    ql_set_error_handler(NULL, NULL);
}

MU_TEST(test_failure_without_handler_is_safe)
{
    ql_allocator_t alloc = ql_default_allocator();

    alloc.malloc = failing_malloc;
    alloc.realloc = failing_realloc;
    alloc.free = failing_free;

    ql_set_error_handler(NULL, NULL);

    /* Must not crash even though nothing is listening. */
    MU_CHECK(ql_alloc(&alloc, 8) == NULL);
    MU_CHECK(ql_realloc(&alloc, NULL, 8) == NULL);
}

MU_TEST(test_failed_realloc_keeps_original_block)
{
    ql_allocator_t alloc = ql_default_allocator();
    handler_state state = {0, QL_OK, NULL, NULL};
    unsigned char* p;

    alloc.realloc = failing_realloc; /* malloc/free stay on libc */

    ql_set_error_handler(record_handler, &state);

    p = ql_alloc(&alloc, 16);
    MU_ASSERT(p != NULL);
    memset(p, 0x5A, 16);

    MU_CHECK(ql_realloc(&alloc, p, 32) == NULL);
    MU_CHECK(state.code == QL_ERR_NOMEM);

    /* The documented contract: the original block is untouched and still ours. */
    MU_CHECK(p[0] == 0x5A);
    MU_CHECK(p[15] == 0x5A);

    ql_dealloc(&alloc, p);
    ql_set_error_handler(NULL, NULL);
}

/* ---------------------------------------------------------------------------
 * Aligned allocation
 * ------------------------------------------------------------------------ */

/** @brief None of the alignments below overflow the padded size. */
static const size_t k_alignments[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 4096};

MU_TEST(test_size_add_overflow)
{
    size_t out = 123;

    MU_CHECK(ql_size_add_overflow(3, 4, &out) == QL_OK);
    MU_CHECK(out == 7);

    MU_CHECK(ql_size_add_overflow(0, SIZE_MAX, &out) == QL_OK);
    MU_CHECK(out == SIZE_MAX);

    /* On failure the result must be left untouched. */
    out = 999;
    MU_CHECK(ql_size_add_overflow(SIZE_MAX, 1, &out) == QL_ERR_OVERFLOW);
    MU_CHECK(out == 999);
    MU_CHECK(ql_size_add_overflow(1, SIZE_MAX, &out) == QL_ERR_OVERFLOW);
    MU_CHECK(ql_size_add_overflow(SIZE_MAX, SIZE_MAX, &out) == QL_ERR_OVERFLOW);

    MU_CHECK(ql_size_add_overflow(1, 1, NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_aligned_alloc_is_aligned_and_writable)
{
    size_t i;
    size_t j;

    for (i = 0; i < sizeof k_alignments / sizeof k_alignments[0]; i++)
    {
        size_t alignment = k_alignments[i];
        unsigned char* p = ql_aligned_alloc(NULL, alignment, 300);

        MU_ASSERT(p != NULL);
        MU_CHECK(((uintptr_t)p % (uintptr_t)alignment) == 0U);

        /*
         * Touch every requested byte with a value derived from the index. ASAN
         * turns a wrong layout -- a header written over the payload, or a block
         * that is too short -- into an immediate failure here.
         */
        for (j = 0; j < 300; j++)
        {
            p[j] = (unsigned char)(j & 0xFFU);
        }
        for (j = 0; j < 300; j++)
        {
            MU_CHECK(p[j] == (unsigned char)(j & 0xFFU));
        }

        ql_aligned_free(NULL, p);
    }
}

MU_TEST(test_aligned_alloc_rejects_bad_alignment)
{
    handler_state state = {0, QL_OK, NULL, NULL};

    ql_set_error_handler(record_handler, &state);

    /* Zero is not a power of two, and neither are these. */
    MU_CHECK(ql_aligned_alloc(NULL, 0, 64) == NULL);
    MU_CHECK(state.calls == 1);
    MU_CHECK(state.code == QL_ERR_INVALID_ARG);

    MU_CHECK(ql_aligned_alloc(NULL, 3, 64) == NULL);
    MU_CHECK(state.calls == 2);
    MU_CHECK(state.code == QL_ERR_INVALID_ARG);

    MU_CHECK(ql_aligned_alloc(NULL, 6, 64) == NULL);
    MU_CHECK(ql_aligned_alloc(NULL, 100, 64) == NULL);
    MU_CHECK(ql_aligned_alloc(NULL, SIZE_MAX, 64) == NULL);
    MU_CHECK(state.calls == 5);
    MU_CHECK(state.code == QL_ERR_INVALID_ARG);

    ql_set_error_handler(NULL, NULL);
}

MU_TEST(test_aligned_alloc_zero_size_is_not_an_error)
{
    handler_state state = {0, QL_OK, NULL, NULL};

    ql_set_error_handler(record_handler, &state);

    MU_CHECK(ql_aligned_alloc(NULL, 64, 0) == NULL);
    MU_CHECK(state.calls == 0); /* no error raised */

    ql_aligned_free(NULL, NULL); /* must be a no-op */
    MU_CHECK(state.calls == 0);

    ql_set_error_handler(NULL, NULL);
}

MU_TEST(test_aligned_alloc_goes_through_the_custom_allocator)
{
    counting_state state = {0, 0, 0};
    ql_allocator_t alloc = ql_default_allocator();
    void* p;

    alloc.malloc = counting_malloc;
    alloc.realloc = counting_realloc;
    alloc.free = counting_free;
    alloc.userdata = &state;

    p = ql_aligned_alloc(&alloc, 64, 128);
    MU_ASSERT(p != NULL);
    MU_CHECK(((uintptr_t)p % 64U) == 0U);
    MU_CHECK(state.mallocs == 1);
    MU_CHECK(state.frees == 0);

    ql_aligned_free(&alloc, p);
    MU_CHECK(state.frees == 1);
    MU_CHECK(state.mallocs == state.frees);
}

MU_TEST(test_aligned_alloc_failure_raises_nomem)
{
    ql_allocator_t alloc = ql_default_allocator();
    handler_state state = {0, QL_OK, NULL, NULL};

    alloc.malloc = failing_malloc;
    alloc.realloc = failing_realloc;
    alloc.free = failing_free;

    ql_set_error_handler(record_handler, &state);

    MU_CHECK(ql_aligned_alloc(&alloc, 32, 64) == NULL);
    MU_CHECK(state.calls == 1);
    MU_CHECK(state.code == QL_ERR_NOMEM);
    MU_CHECK(state.message != NULL);

    ql_set_error_handler(NULL, NULL);
}

MU_TEST(test_aligned_alloc_rejects_padded_overflow)
{
    handler_state state = {0, QL_OK, NULL, NULL};

    ql_set_error_handler(record_handler, &state);

    /*
     * The padding (alignment + sizeof(void*)) is what overflows, not the user's
     * size on its own, so this also checks the addition is done on the padded
     * total rather than only on `size`.
     */
    MU_CHECK(ql_aligned_alloc(NULL, 4096, SIZE_MAX - 8) == NULL);
    MU_CHECK(state.calls == 1);
    MU_CHECK(state.code == QL_ERR_OVERFLOW);

    ql_set_error_handler(NULL, NULL);
}

MU_TEST(test_aligned_blocks_are_distinct)
{
    unsigned char* a = ql_aligned_alloc(NULL, 64, 64);
    unsigned char* b = ql_aligned_alloc(NULL, 64, 64);
    unsigned char* c = ql_aligned_alloc(NULL, 64, 64);
    size_t i;

    MU_ASSERT(a != NULL);
    MU_ASSERT(b != NULL);
    MU_ASSERT(c != NULL);

    MU_CHECK(a != b);
    MU_CHECK(b != c);
    MU_CHECK(a != c);

    /* Each block carries its own header, so writing one must not touch another. */
    memset(a, 0xAA, 64);
    memset(b, 0xBB, 64);
    memset(c, 0xCC, 64);
    for (i = 0; i < 64; i++)
    {
        MU_CHECK(a[i] == 0xAA);
        MU_CHECK(b[i] == 0xBB);
        MU_CHECK(c[i] == 0xCC);
    }

    ql_aligned_free(NULL, a);
    ql_aligned_free(NULL, b);
    ql_aligned_free(NULL, c);
}

MU_TEST_SUITE(test_suite_mem)
{
    MU_RUN_TEST(test_default_allocator_is_complete);
    MU_RUN_TEST(test_alloc_and_dealloc);
    MU_RUN_TEST(test_calloc_zero_fills);
    MU_RUN_TEST(test_realloc_preserves_contents);
    MU_RUN_TEST(test_realloc_zero_frees);
    MU_RUN_TEST(test_custom_allocator_is_actually_used);
    MU_RUN_TEST(test_size_mul_overflow);
    MU_RUN_TEST(test_alloc_array_happy_path);
    MU_RUN_TEST(test_array_helpers_reject_overflow);
    MU_RUN_TEST(test_failure_raises_nomem);
    MU_RUN_TEST(test_failure_without_handler_is_safe);
    MU_RUN_TEST(test_failed_realloc_keeps_original_block);
    MU_RUN_TEST(test_size_add_overflow);
    MU_RUN_TEST(test_aligned_alloc_is_aligned_and_writable);
    MU_RUN_TEST(test_aligned_alloc_rejects_bad_alignment);
    MU_RUN_TEST(test_aligned_alloc_zero_size_is_not_an_error);
    MU_RUN_TEST(test_aligned_alloc_goes_through_the_custom_allocator);
    MU_RUN_TEST(test_aligned_alloc_failure_raises_nomem);
    MU_RUN_TEST(test_aligned_alloc_rejects_padded_overflow);
    MU_RUN_TEST(test_aligned_blocks_are_distinct);
}
