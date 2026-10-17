#include "test_suites.h"

#include "qlib/ql_hash.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_alloc.h"

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/** @brief A key holding two integers: eight bytes, no padding. */
typedef struct
{
    uint32_t first;
    uint32_t second;
} pair_key;

/** @brief An error-handler probe, as in the other suites. */
typedef struct
{
    int calls;
    ql_ret_t code;
} hash_probe;

static hash_probe g_probe;

static void hash_probe_record(ql_ret_t code, const char* message, void* userdata)
{
    hash_probe* probe = userdata;

    (void)message;
    probe->calls++;
    probe->code = code;
}

static void probe_begin(void)
{
    g_probe.calls = 0;
    g_probe.code = QL_OK;
    ql_set_error_handler(hash_probe_record, &g_probe);
}

static void probe_expect(int calls)
{
    if (g_probe.calls != calls)
    {
        fprintf(stderr, "  handler called %d time(s), expected %d\n", g_probe.calls, calls);
    }
    MU_CHECK(g_probe.calls == calls);
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

MU_TEST(test_create_rejects_bad_arguments)
{
    ql_hash_t* hash = (ql_hash_t*)&g_probe;

    MU_CHECK(ql_hash_create(NULL, 0, sizeof(int), &hash) == QL_ERR_INVALID_ARG);
    MU_CHECK(hash == NULL);

    MU_CHECK(ql_hash_create(NULL, sizeof(int), sizeof(int), NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_lifecycle)
{
    ql_hash_t* hash = NULL;

    MU_ASSERT(ql_hash_create(NULL, sizeof(uint32_t), sizeof(uint64_t), &hash) == QL_OK);
    MU_CHECK(ql_hash_size(hash) == 0);
    MU_CHECK(ql_hash_capacity(hash) >= 8);
    MU_CHECK(ql_hash_key_size(hash) == sizeof(uint32_t));
    MU_CHECK(ql_hash_value_size(hash) == sizeof(uint64_t));

    ql_hash_destroy(hash);
    ql_hash_destroy(NULL);

    MU_CHECK(ql_hash_size(NULL) == 0);
    MU_CHECK(ql_hash_capacity(NULL) == 0);
    MU_CHECK(ql_hash_key_size(NULL) == 0);
    MU_CHECK(ql_hash_value_size(NULL) == 0);
    MU_CHECK(!ql_hash_contains(NULL, NULL));
}

MU_TEST(test_set_and_get)
{
    ql_hash_t* hash = NULL;
    uint32_t key = 42;
    uint64_t value = 1234567890123ULL;
    uint64_t out = 0;

    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof value, &hash) == QL_OK);

    MU_CHECK(ql_hash_get(hash, &key, &out) == QL_ERR_NOT_FOUND);
    MU_CHECK(out == 0);

    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    MU_CHECK(ql_hash_size(hash) == 1);
    MU_CHECK(ql_hash_contains(hash, &key));

    MU_ASSERT(ql_hash_get(hash, &key, &out) == QL_OK);
    MU_CHECK(out == value);

    ql_hash_destroy(hash);
}

MU_TEST(test_set_overwrites_without_growing_the_count)
{
    ql_hash_t* hash = NULL;
    uint32_t key = 7;
    uint32_t value = 1;
    uint32_t out = 0;

    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof value, &hash) == QL_OK);

    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    value = 2;
    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    value = 3;
    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);

    MU_CHECK(ql_hash_size(hash) == 1);
    MU_ASSERT(ql_hash_get(hash, &key, &out) == QL_OK);
    MU_CHECK(out == 3);

    ql_hash_destroy(hash);
}

MU_TEST(test_struct_and_string_keys)
{
    ql_hash_t* pairs = NULL;
    ql_hash_t* strings = NULL;
    pair_key key = {1, 2};
    pair_key other = {1, 3};
    int value = 10;
    int out = 0;
    char name[8];

    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof value, &pairs) == QL_OK);
    MU_ASSERT(ql_hash_set(pairs, &key, &value) == QL_OK);
    MU_ASSERT(ql_hash_get(pairs, &key, &out) == QL_OK);
    MU_CHECK(out == 10);
    MU_CHECK(!ql_hash_contains(pairs, &other));
    MU_CHECK(ql_hash_get(pairs, &other, &out) == QL_ERR_NOT_FOUND);

    /* Fixed size string keys: the whole fixed buffer is the key, so unused
     * bytes have to be zeroed by the caller. */
    MU_ASSERT(ql_hash_create(NULL, sizeof name, sizeof value, &strings) == QL_OK);
    memset(name, 0, sizeof name);
    memcpy(name, "alpha", sizeof "alpha");
    value = 1;
    MU_ASSERT(ql_hash_set(strings, &name, &value) == QL_OK);

    memset(name, 0, sizeof name);
    memcpy(name, "beta", sizeof "beta");
    value = 2;
    MU_ASSERT(ql_hash_set(strings, &name, &value) == QL_OK);

    memset(name, 0, sizeof name);
    memcpy(name, "alpha", sizeof "alpha");
    MU_ASSERT(ql_hash_get(strings, &name, &out) == QL_OK);
    MU_CHECK(out == 1);

    memset(name, 0, sizeof name);
    memcpy(name, "gamma", sizeof "gamma");
    MU_CHECK(ql_hash_get(strings, &name, &out) == QL_ERR_NOT_FOUND);

    ql_hash_destroy(strings);
    ql_hash_destroy(pairs);
}

MU_TEST(test_value_size_zero_is_a_membership_set)
{
    ql_hash_t* hash = NULL;
    uint32_t key = 5;
    uint32_t absent = 6;

    MU_ASSERT(ql_hash_create(NULL, sizeof key, 0, &hash) == QL_OK);
    MU_CHECK(ql_hash_value_size(hash) == 0);

    MU_ASSERT(ql_hash_set(hash, &key, NULL) == QL_OK);
    MU_CHECK(ql_hash_contains(hash, &key));
    MU_CHECK(ql_hash_get(hash, &key, NULL) == QL_OK);
    MU_CHECK(!ql_hash_contains(hash, &absent));
    MU_CHECK(ql_hash_get(hash, &absent, NULL) == QL_ERR_NOT_FOUND);
    MU_CHECK(ql_hash_size(hash) == 1);

    /* With a value size, a NULL value is a bad argument. */
    ql_hash_destroy(hash);
    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof(uint32_t), &hash) == QL_OK);
    MU_CHECK(ql_hash_set(hash, &key, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_hash_get(hash, &key, NULL) == QL_ERR_INVALID_ARG);
    ql_hash_destroy(hash);
}

MU_TEST(test_remove_and_reinsert)
{
    ql_hash_t* hash = NULL;
    uint32_t key = 11;
    uint32_t value = 100;
    uint32_t out = 0;
    uint32_t absent = 12;

    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof value, &hash) == QL_OK);

    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    MU_ASSERT(ql_hash_remove(hash, &key) == QL_OK);
    MU_CHECK(ql_hash_size(hash) == 0);
    MU_CHECK(!ql_hash_contains(hash, &key));
    MU_CHECK(ql_hash_get(hash, &key, &out) == QL_ERR_NOT_FOUND);

    /* Removing again reports that it is gone. */
    MU_CHECK(ql_hash_remove(hash, &key) == QL_ERR_NOT_FOUND);
    MU_CHECK(ql_hash_remove(hash, &absent) == QL_ERR_NOT_FOUND);

    /* The tombstone must be reusable. */
    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    MU_CHECK(ql_hash_size(hash) == 1);
    MU_ASSERT(ql_hash_get(hash, &key, &out) == QL_OK);
    MU_CHECK(out == 100);

    ql_hash_destroy(hash);
}

MU_TEST(test_many_entries_force_growth)
{
    enum
    {
        COUNT = 1000,
    };
    ql_hash_t* hash = NULL;
    uint32_t i;

    MU_ASSERT(ql_hash_create(NULL, sizeof(uint32_t), sizeof(uint32_t), &hash) == QL_OK);

    for (i = 0; i < COUNT; i++)
    {
        uint32_t key = i;
        uint32_t value = i * 7U;

        MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    }
    MU_CHECK(ql_hash_size(hash) == COUNT);
    /* Growth keeps the load factor below three quarters. */
    MU_CHECK(ql_hash_capacity(hash) > COUNT);

    for (i = 0; i < COUNT; i++)
    {
        uint32_t key = i;
        uint32_t out = 0;

        if (ql_hash_get(hash, &key, &out) != QL_OK)
        {
            fprintf(stderr, "  key %u went missing\n", (unsigned)i);
        }
        MU_CHECK(ql_hash_get(hash, &key, &out) == QL_OK);
        MU_CHECK(out == (i * 7U));
    }

    ql_hash_destroy(hash);
}

MU_TEST(test_delete_and_reinsert_rounds)
{
    enum
    {
        PER_ROUND = 200,
        ROUNDS = 8,
    };
    ql_hash_t* hash = NULL;
    uint32_t round;

    MU_ASSERT(ql_hash_create(NULL, sizeof(uint32_t), sizeof(uint32_t), &hash) == QL_OK);

    /*
     * Deleting half of a populated table and refilling it is what exercises
     * tombstones: a lookup for a key that lives past a tombstone must still
     * find it, and insertion must reuse the tombstones rather than leaking
     * capacity.
     */
    for (round = 0; round < ROUNDS; round++)
    {
        uint32_t i;

        for (i = 0; i < PER_ROUND; i++)
        {
            uint32_t key = i;
            uint32_t value = (round * PER_ROUND) + i;

            MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
        }
        MU_CHECK(ql_hash_size(hash) == PER_ROUND);

        for (i = 0; i < PER_ROUND; i += 2)
        {
            uint32_t key = i;

            MU_ASSERT(ql_hash_remove(hash, &key) == QL_OK);
        }
        MU_CHECK(ql_hash_size(hash) == PER_ROUND / 2);

        for (i = 0; i < PER_ROUND; i++)
        {
            uint32_t key = i;
            uint32_t out = 0;
            bool present = ql_hash_contains(hash, &key);

            MU_CHECK(present == (i % 2 != 0));
            if (present)
            {
                MU_ASSERT(ql_hash_get(hash, &key, &out) == QL_OK);
                MU_CHECK(out == ((round * PER_ROUND) + i));
            }
        }

        ql_hash_clear(hash);
        MU_CHECK(ql_hash_size(hash) == 0);
    }

    ql_hash_destroy(hash);
}

MU_TEST(test_iteration_visits_every_entry_once)
{
    enum
    {
        COUNT = 64,
    };
    ql_hash_t* hash = NULL;
    ql_hash_iter_t it;
    unsigned seen[COUNT];
    uint32_t i;
    size_t visited = 0;

    memset(seen, 0, sizeof seen);
    MU_ASSERT(ql_hash_create(NULL, sizeof(uint32_t), sizeof(uint32_t), &hash) == QL_OK);

    for (i = 0; i < COUNT; i++)
    {
        uint32_t key = i;
        uint32_t value = i;

        MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    }
    /* Leave a few tombstones to make sure they are skipped. */
    for (i = 0; i < COUNT; i += 8)
    {
        uint32_t key = i;

        MU_ASSERT(ql_hash_remove(hash, &key) == QL_OK);
    }

    ql_hash_iter_init(&it, hash);
    for (;;)
    {
        const void* raw_key = NULL;
        const void* raw_value = NULL;
        uint32_t key;
        uint32_t value;

        if (!ql_hash_iter_next(&it, &raw_key, &raw_value))
        {
            break;
        }
        MU_ASSERT(raw_key != NULL && raw_value != NULL);
        memcpy(&key, raw_key, sizeof key);
        memcpy(&value, raw_value, sizeof value);

        MU_ASSERT(key < COUNT);
        MU_CHECK(seen[key] == 0);
        seen[key] = 1;
        visited++;
    }

    MU_CHECK(visited == ql_hash_size(hash));
    for (i = 0; i < COUNT; i++)
    {
        MU_CHECK(seen[i] == (i % 8 != 0 ? 1U : 0U));
    }

    /* Iterating NULL yields nothing rather than crashing. */
    ql_hash_iter_init(&it, NULL);
    MU_CHECK(!ql_hash_iter_next(&it, NULL, NULL));
    ql_hash_iter_init(NULL, hash);

    ql_hash_destroy(hash);
}

MU_TEST(test_clear_and_reuse)
{
    ql_hash_t* hash = NULL;
    uint32_t key = 1;
    uint32_t value = 2;
    uint32_t out = 0;
    size_t capacity;

    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof value, &hash) == QL_OK);
    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    capacity = ql_hash_capacity(hash);

    ql_hash_clear(hash);
    MU_CHECK(ql_hash_size(hash) == 0);
    MU_CHECK(ql_hash_capacity(hash) == capacity);
    MU_CHECK(!ql_hash_contains(hash, &key));

    ql_hash_clear(NULL);

    MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    MU_ASSERT(ql_hash_get(hash, &key, &out) == QL_OK);
    MU_CHECK(out == 2);

    ql_hash_destroy(hash);
}

MU_TEST(test_null_arguments_are_rejected)
{
    ql_hash_t* hash = NULL;
    uint32_t key = 1;
    uint32_t value = 2;
    uint32_t out = 0;
    ql_hash_iter_t it;

    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof value, &hash) == QL_OK);

    MU_CHECK(ql_hash_set(NULL, &key, &value) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_hash_set(hash, NULL, &value) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_hash_get(NULL, &key, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_hash_get(hash, NULL, &out) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_hash_remove(NULL, &key) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_hash_remove(hash, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(!ql_hash_contains(hash, NULL));
    MU_CHECK(!ql_hash_contains(NULL, &key));

    ql_hash_iter_init(&it, hash);
    MU_CHECK(!ql_hash_iter_next(NULL, NULL, NULL));

    ql_hash_destroy(hash);
}

MU_TEST(test_no_leaks)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_hash_t* hash = NULL;
    uint32_t i;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    MU_ASSERT(ql_hash_create(&alloc, sizeof(uint32_t), sizeof(uint32_t), &hash) == QL_OK);
    for (i = 0; i < 100; i++)
    {
        uint32_t key = i;
        uint32_t value = i;

        MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    }
    ql_hash_destroy(hash);

    MU_CHECK(state.live_blocks == 0);
    MU_CHECK(state.total_reallocs == 0); /* growth uses fresh allocations */
    MU_CHECK(state.total_allocs > 3);    /* struct plus three arrays per growth */
}

MU_TEST(test_allocation_failure_is_reported)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_hash_t* hash = NULL;
    uint32_t i;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);
    MU_ASSERT(ql_hash_create(&alloc, sizeof(uint32_t), sizeof(uint32_t), &hash) == QL_OK);
    MU_CHECK(state.live_blocks == 4); /* the table plus three arrays */

    /*
     * The initial capacity is 8 slots and growth is due on the seventh insert,
     * so six entries fit without allocating and the seventh needs a rehash.
     */
    for (i = 0; i < 6; i++)
    {
        uint32_t key = i;
        uint32_t value = i;

        MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
    }
    MU_CHECK(state.live_blocks == 4);

    state.fail_at = state.alloc_calls + 1;
    {
        uint32_t key = 6;
        uint32_t value = 6;

        MU_CHECK(ql_hash_set(hash, &key, &value) == QL_ERR_NOMEM);
    }

    /* The table is intact: nothing was lost and it still answers queries. */
    MU_CHECK(state.live_blocks == 4);
    MU_CHECK(ql_hash_size(hash) == 6);
    for (i = 0; i < 6; i++)
    {
        uint32_t key = i;
        uint32_t out = 99;

        MU_ASSERT(ql_hash_get(hash, &key, &out) == QL_OK);
        MU_CHECK(out == i);
    }

    /* And the same insert succeeds once allocations are allowed again. */
    state.fail_at = 0;
    {
        uint32_t key = 6;
        uint32_t value = 6;

        MU_ASSERT(ql_hash_set(hash, &key, &value) == QL_OK);
        MU_CHECK(ql_hash_contains(hash, &key));
        MU_CHECK(ql_hash_size(hash) == 7);
        /* A successful rehash frees the three old arrays, so the live count is
         * back to the table plus three arrays, at the larger capacity. */
        MU_CHECK(state.live_blocks == 4);
    }

    ql_hash_destroy(hash);
    MU_CHECK(state.live_blocks == 0);
}

MU_TEST(test_failures_are_raised)
{
    ql_hash_t* hash = NULL;
    uint32_t key = 1;
    uint32_t out = 0;

    MU_ASSERT(ql_hash_create(NULL, sizeof key, sizeof key, &hash) == QL_OK);

    probe_begin();
    MU_CHECK(ql_hash_set(NULL, &key, &key) == QL_ERR_INVALID_ARG);
    probe_expect(1);

    /* A miss is not an error worth reporting. */
    MU_CHECK(ql_hash_get(hash, &key, &out) == QL_ERR_NOT_FOUND);
    probe_expect(1);

    MU_CHECK(ql_hash_remove(hash, &key) == QL_ERR_NOT_FOUND);
    probe_expect(1);

    {
        /* A separate out parameter: a failed create sets *out to NULL, so
         * reusing `hash` here would orphan the table that is still live. */
        ql_hash_t* rejected = (ql_hash_t*)&g_probe;

        MU_CHECK(ql_hash_create(NULL, 0, 0, &rejected) == QL_ERR_INVALID_ARG);
        MU_CHECK(rejected == NULL);
    }
    probe_expect(2);

    ql_set_error_handler(NULL, NULL);
    ql_hash_destroy(hash);
}

MU_TEST_SUITE(test_suite_hash)
{
    MU_RUN_TEST(test_create_rejects_bad_arguments);
    MU_RUN_TEST(test_lifecycle);
    MU_RUN_TEST(test_set_and_get);
    MU_RUN_TEST(test_set_overwrites_without_growing_the_count);
    MU_RUN_TEST(test_struct_and_string_keys);
    MU_RUN_TEST(test_value_size_zero_is_a_membership_set);
    MU_RUN_TEST(test_remove_and_reinsert);
    MU_RUN_TEST(test_many_entries_force_growth);
    MU_RUN_TEST(test_delete_and_reinsert_rounds);
    MU_RUN_TEST(test_iteration_visits_every_entry_once);
    MU_RUN_TEST(test_clear_and_reuse);
    MU_RUN_TEST(test_null_arguments_are_rejected);
    MU_RUN_TEST(test_no_leaks);
    MU_RUN_TEST(test_allocation_failure_is_reported);
    MU_RUN_TEST(test_failures_are_raised);
}
