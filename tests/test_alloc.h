#ifndef QLIB_TEST_ALLOC_H
#define QLIB_TEST_ALLOC_H

/*
 * Allocators shared by the test suites.
 *
 * Extracted from test_vector.c when three more suites needed the same thing:
 * four copies of a test fixture is how the package job's consumer drifted out
 * of step with the one used locally, and test helpers are no different.
 *
 * Everything here is `static`, so including this from several translation units
 * is fine.
 */

#include "qlib/ql_mem.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    size_t size;
} block_header;

typedef struct
{
    size_t live_blocks;
    size_t total_allocs;
    size_t total_reallocs;
    /** 1-based index of the allocation that should fail; 0 means never. */
    size_t fail_at;
    /** Allocations attempted so far, failed ones included. */
    size_t alloc_calls;
} tracking_state;

/** @brief Zero a tracking state, including the fields added later. */
static void tracking_init(tracking_state* state)
{
    memset(state, 0, sizeof *state);
}

static void* tracking_malloc(size_t size, void* userdata)
{
    tracking_state* state = userdata;
    block_header* header;

    state->alloc_calls++;
    if (state->fail_at != 0 && state->alloc_calls == state->fail_at)
    {
        return NULL;
    }

    header = malloc(sizeof *header + size);
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

    /* Deliberately malloc+copy+free instead of realloc, so the block moves.
     * With libc realloc a growing block may stay put, which would hide a
     * use-after-free when the caller passes a pointer into its own buffer. */
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

#endif /* QLIB_TEST_ALLOC_H */
