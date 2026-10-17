#include "test_suites.h"

#include "qlib/ql_thread.h"

#include <stdio.h>
#include <string.h>

#include "test_alloc.h"

/*
 * Threading tests.
 *
 * Everything that waits has a timeout, so a synchronisation bug fails the test
 * instead of hanging CI. That matters more here than anywhere else in the suite:
 * a broken condition variable would otherwise block until the job is killed.
 */

#define WAIT_LIMIT_MS 5000

/* A sentinel for "the function must not have written through this pointer". */
static int g_probe;

/* A thread body that needs no shared state, for the leak test. */
static void do_nothing(void* arg)
{
    (void)arg;
}
#define WORKERS 4
#define INCREMENTS 10000

/* ---------------------------------------------------------------------------
 * Shared state for the tests below.
 * ------------------------------------------------------------------------ */

typedef struct
{
    ql_mutex_t* mutex;
    ql_cond_t* cond;
    int counter;
    int finished;
} shared_t;

static void increment_worker(void* arg)
{
    shared_t* shared = arg;
    int i;

    for (i = 0; i < INCREMENTS; i++)
    {
        ql_mutex_lock(shared->mutex);
        shared->counter++;
        ql_mutex_unlock(shared->mutex);
    }
}

static void waiter(void* arg)
{
    shared_t* shared = arg;

    ql_mutex_lock(shared->mutex);
    while (!shared->finished)
    {
        if (ql_cond_wait(shared->cond, shared->mutex, WAIT_LIMIT_MS) != QL_OK)
        {
            break;
        }
    }
    shared->counter++;
    ql_mutex_unlock(shared->mutex);
}

/** @brief Set the flag under the mutex and touch nothing else. */
static void mark_finished(void* arg)
{
    shared_t* shared = arg;

    ql_mutex_lock(shared->mutex);
    shared->finished = 1;
    ql_mutex_unlock(shared->mutex);
}

static void signaller(void* arg)
{
    shared_t* shared = arg;

    ql_mutex_lock(shared->mutex);
    shared->finished = 1;
    ql_mutex_unlock(shared->mutex);
    ql_cond_broadcast(shared->cond);
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

MU_TEST(test_create_rejects_bad_arguments)
{
    ql_thread_t* thread = (ql_thread_t*)&g_probe;
    ql_mutex_t* mutex = (ql_mutex_t*)&g_probe;
    ql_cond_t* cond = (ql_cond_t*)&g_probe;

    MU_CHECK(ql_thread_create(NULL, NULL, NULL, &thread) == QL_ERR_INVALID_ARG);
    MU_CHECK(thread == NULL);
    MU_CHECK(ql_thread_create(NULL, increment_worker, NULL, NULL) == QL_ERR_INVALID_ARG);

    MU_CHECK(ql_mutex_create(NULL, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_cond_create(NULL, NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_mutex_create(NULL, &mutex) == QL_OK);
    MU_CHECK(ql_cond_create(NULL, &cond) == QL_OK);
    MU_CHECK(ql_cond_wait(NULL, mutex, 10) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_cond_wait(cond, NULL, 10) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_cond_wait(cond, mutex, -1) == QL_ERR_INVALID_ARG);

    ql_cond_destroy(cond);
    ql_mutex_destroy(mutex);

    /* Destroying NULL is a no-op everywhere. */
    ql_thread_join(NULL);
    ql_thread_detach(NULL);
    ql_mutex_destroy(NULL);
    ql_cond_destroy(NULL);
    ql_mutex_lock(NULL);
    ql_mutex_unlock(NULL);
    ql_cond_signal(NULL);
    ql_cond_broadcast(NULL);
}

MU_TEST(test_a_thread_runs_and_joins)
{
    shared_t shared;
    ql_thread_t* thread = NULL;

    memset(&shared, 0, sizeof shared);
    MU_ASSERT(ql_thread_create(NULL, increment_worker, &shared, &thread) == QL_OK);
    MU_CHECK(thread != NULL);
    ql_thread_join(thread);

    /* A single worker with no mutex still does all its increments. */
    MU_CHECK(shared.counter == INCREMENTS);
}

MU_TEST(test_a_mutex_serialises_increments)
{
    shared_t shared;
    ql_thread_t* threads[WORKERS];
    int i;

    memset(&shared, 0, sizeof shared);
    MU_ASSERT(ql_mutex_create(NULL, &shared.mutex) == QL_OK);

    for (i = 0; i < WORKERS; i++)
    {
        threads[i] = NULL;
        MU_ASSERT(ql_thread_create(NULL, increment_worker, &shared, &threads[i]) == QL_OK);
    }
    for (i = 0; i < WORKERS; i++)
    {
        ql_thread_join(threads[i]);
    }

    /* Without the mutex this is a read-modify-write race and the count falls
     * short; with it the total is exact. */
    MU_CHECK(shared.counter == WORKERS * INCREMENTS);

    ql_mutex_destroy(shared.mutex);
}

MU_TEST(test_cond_wait_is_woken_by_a_signal)
{
    shared_t shared;
    ql_thread_t* waiting = NULL;
    ql_thread_t* notifier = NULL;

    memset(&shared, 0, sizeof shared);
    MU_ASSERT(ql_mutex_create(NULL, &shared.mutex) == QL_OK);
    MU_ASSERT(ql_cond_create(NULL, &shared.cond) == QL_OK);

    MU_ASSERT(ql_thread_create(NULL, waiter, &shared, &waiting) == QL_OK);
    MU_ASSERT(ql_thread_create(NULL, signaller, &shared, &notifier) == QL_OK);

    ql_thread_join(notifier);
    ql_thread_join(waiting);

    MU_CHECK(shared.counter == 1);
    MU_CHECK(shared.finished == 1);

    ql_cond_destroy(shared.cond);
    ql_mutex_destroy(shared.mutex);
}

MU_TEST(test_cond_wait_times_out)
{
    ql_mutex_t* mutex = NULL;
    ql_cond_t* cond = NULL;

    MU_ASSERT(ql_mutex_create(NULL, &mutex) == QL_OK);
    MU_ASSERT(ql_cond_create(NULL, &cond) == QL_OK);

    ql_mutex_lock(mutex);
    /* Nothing will signal this, so the wait must expire and say so. */
    MU_CHECK(ql_cond_wait(cond, mutex, 50) == QL_ERR_TIMEOUT);
    ql_mutex_unlock(mutex);

    /* The mutex is still usable afterwards. */
    ql_mutex_lock(mutex);
    ql_mutex_unlock(mutex);

    ql_cond_destroy(cond);
    ql_mutex_destroy(mutex);
}

MU_TEST(test_broadcast_wakes_every_waiter)
{
    shared_t shared;
    ql_thread_t* waiters[WORKERS];
    ql_thread_t* notifier = NULL;
    int i;

    memset(&shared, 0, sizeof shared);
    MU_ASSERT(ql_mutex_create(NULL, &shared.mutex) == QL_OK);
    MU_ASSERT(ql_cond_create(NULL, &shared.cond) == QL_OK);

    for (i = 0; i < WORKERS; i++)
    {
        waiters[i] = NULL;
        MU_ASSERT(ql_thread_create(NULL, waiter, &shared, &waiters[i]) == QL_OK);
    }

    /*
     * Giving the waiters a moment to reach the wait is not a correctness
     * requirement -- the predicate is checked before waiting, so a waiter that
     * arrives late sees finished == 1 and does not wait at all.
     */
    MU_ASSERT(ql_thread_create(NULL, signaller, &shared, &notifier) == QL_OK);

    ql_thread_join(notifier);
    for (i = 0; i < WORKERS; i++)
    {
        ql_thread_join(waiters[i]);
    }

    MU_CHECK(shared.counter == WORKERS);

    ql_cond_destroy(shared.cond);
    ql_mutex_destroy(shared.mutex);
}

MU_TEST(test_detached_thread_is_not_joined)
{
    shared_t shared;
    ql_thread_t* thread = NULL;
    int done = 0;
    int guard;

    memset(&shared, 0, sizeof shared);
    MU_ASSERT(ql_mutex_create(NULL, &shared.mutex) == QL_OK);

    /*
     * Deliberately no condition variable in this test. A detached thread cannot
     * be joined, so there is no way to know it has finished -- only that it has
     * done its work. Touching the mutex is therefore the last thing it is
     * allowed to do, and main only observes `finished` by acquiring that same
     * mutex, which it cannot do until the thread has released it.
     *
     * The first version of this test had the detached thread call
     * ql_cond_broadcast() after unlocking, and both ASAN and ThreadSanitizer
     * flagged main destroying the condition variable underneath it.
     */
    MU_ASSERT(ql_thread_create(NULL, mark_finished, &shared, &thread) == QL_OK);
    ql_thread_detach(thread);

    /* Poll under the mutex; the detached thread holds it only briefly. */
    for (guard = 0; guard < 1000000 && !done; guard++)
    {
        ql_mutex_lock(shared.mutex);
        done = shared.finished;
        ql_mutex_unlock(shared.mutex);
    }

    MU_CHECK(done == 1);
    ql_mutex_destroy(shared.mutex);
}

MU_TEST(test_hardware_concurrency_is_at_least_one)
{
    unsigned count = ql_thread_hardware_concurrency();

    MU_CHECK(count >= 1);
    if (count > 64)
    {
        fprintf(stderr, "  note: hardware_concurrency reported %u\n", count);
    }
}

MU_TEST(test_no_leaks)
{
    tracking_state state;
    ql_allocator_t alloc;
    ql_thread_t* thread = NULL;
    ql_mutex_t* mutex = NULL;
    ql_cond_t* cond = NULL;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    MU_ASSERT(ql_mutex_create(&alloc, &mutex) == QL_OK);
    MU_ASSERT(ql_cond_create(&alloc, &cond) == QL_OK);
    MU_ASSERT(ql_thread_create(&alloc, do_nothing, NULL, &thread) == QL_OK);

    /*
     * Join before reading the counter. A thread frees its own start context, so
     * the live count depends on whether it has got that far -- reading it while
     * the thread runs is a data race, and ThreadSanitizer says so. Joining
     * creates the happens-before edge that makes the read meaningful.
     *
     * The tracking allocator is not thread-safe either, which is another reason
     * this is the only test that hands it to a thread.
     */
    ql_thread_join(thread);
    MU_CHECK(state.live_blocks == 2); /* the mutex and the condition variable */

    ql_cond_destroy(cond);
    ql_mutex_destroy(mutex);

    MU_CHECK(state.live_blocks == 0);
}

MU_TEST_SUITE(test_suite_thread)
{
    MU_RUN_TEST(test_create_rejects_bad_arguments);
    MU_RUN_TEST(test_a_thread_runs_and_joins);
    MU_RUN_TEST(test_a_mutex_serialises_increments);
    MU_RUN_TEST(test_cond_wait_is_woken_by_a_signal);
    MU_RUN_TEST(test_cond_wait_times_out);
    MU_RUN_TEST(test_broadcast_wakes_every_waiter);
    MU_RUN_TEST(test_detached_thread_is_not_joined);
    MU_RUN_TEST(test_hardware_concurrency_is_at_least_one);
    MU_RUN_TEST(test_no_leaks);
}
