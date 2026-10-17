#include "qlib/ql_thread.h"

#include "ql_internal.h"

/*
 * Two implementations, one API.
 *
 *   POSIX                       Windows
 *   pthread_t                   HANDLE from _beginthreadex
 *   pthread_mutex_t             CRITICAL_SECTION
 *   pthread_cond_t              CONDITION_VARIABLE
 *   pthread_cond_timedwait      SleepConditionVariableCS
 *   sysconf(_SC_NPROCESSORS_ONLN)  GetSystemInfo
 *
 * The Windows thread is started with _beginthreadex rather than CreateThread,
 * because a thread that runs C runtime code must let the CRT initialise and tear
 * down its per-thread state; CreateThread skips that and leaks it.
 */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <process.h>
#  include <windows.h>
typedef HANDLE ql_thread_handle_t;
typedef CRITICAL_SECTION ql_mutex_storage_t;
typedef CONDITION_VARIABLE ql_cond_storage_t;
#else
#  include <errno.h>
#  include <pthread.h>
#  include <time.h>
#  include <unistd.h>
typedef pthread_t ql_thread_handle_t;
typedef pthread_mutex_t ql_mutex_storage_t;
typedef pthread_cond_t ql_cond_storage_t;
#endif

struct ql_thread
{
    ql_allocator_t alloc;
    ql_thread_handle_t handle;
};

/*
 * What the new thread needs at its start, owned by the new thread and freed by
 * the trampoline when the body returns.
 *
 * This is a second allocation on purpose. Putting fn and arg in the ql_thread
 * handle means either the trampoline reads a block that ql_thread_detach() has
 * already freed -- a use-after-free that ASAN caught on the first
 * implementation -- or the caller cannot read the platform handle without
 * racing the thread's own cleanup. Two blocks with one owner each is the
 * version that has neither problem.
 */
typedef struct
{
    ql_allocator_t alloc;
    ql_thread_fn fn;
    void* arg;
} ql_thread_start_t;

struct ql_mutex
{
    ql_allocator_t alloc;
    ql_mutex_storage_t storage;
};

struct ql_cond
{
    ql_allocator_t alloc;
    ql_cond_storage_t storage;
};

/* ---------------------------------------------------------------------------
 * Threads
 * ------------------------------------------------------------------------ */

/** @brief Run the body, then release the start context. */
static void ql_thread_run(ql_thread_start_t* start)
{
    ql_allocator_t alloc = start->alloc;
    ql_thread_fn fn = start->fn;
    void* arg = start->arg;

    /* Copy everything out first: the context is freed below, and the allocator
     * has to be readable until the free completes. */
    ql_dealloc(&alloc, start);
    fn(arg);
}

#ifdef _WIN32
static unsigned __stdcall ql_thread_trampoline(void* raw)
{
    ql_thread_run(raw);
    return 0;
}
#else
static void* ql_thread_trampoline(void* raw)
{
    ql_thread_run(raw);
    return NULL;
}
#endif

ql_ret_t ql_thread_create(const ql_allocator_t* alloc, ql_thread_fn fn, void* arg, ql_thread_t** out)
{
    ql_thread_t* thread;
    ql_thread_start_t* start;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_thread_create: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    if (fn == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_thread_create: fn is NULL");
        return QL_ERR_INVALID_ARG;
    }

    thread = ql_alloc(alloc, sizeof *thread);
    if (thread == NULL)
    {
        return QL_ERR_NOMEM;
    }

    thread->alloc = alloc != NULL ? *alloc : ql_default_allocator();

    start = ql_alloc(alloc, sizeof *start);
    if (start == NULL)
    {
        ql_dealloc(&thread->alloc, thread);
        return QL_ERR_NOMEM;
    }
    start->alloc = thread->alloc;
    start->fn = fn;
    start->arg = arg;

#ifdef _WIN32
    {
        uintptr_t handle = _beginthreadex(NULL, 0, ql_thread_trampoline, start, 0, NULL);

        if (handle == 0)
        {
            ql_dealloc(&thread->alloc, start);
            ql_dealloc(&thread->alloc, thread);
            ql_err_raise(QL_ERR_SYS, "ql_thread_create: _beginthreadex failed");
            return QL_ERR_SYS;
        }
        thread->handle = (HANDLE)handle;
    }
#else
    if (pthread_create(&thread->handle, NULL, ql_thread_trampoline, start) != 0)
    {
        ql_dealloc(&thread->alloc, start);
        ql_dealloc(&thread->alloc, thread);
        ql_err_raise(QL_ERR_SYS, "ql_thread_create: pthread_create failed");
        return QL_ERR_SYS;
    }
#endif

    *out = thread;
    return QL_OK;
}

void ql_thread_join(ql_thread_t* thread)
{
    ql_allocator_t alloc;

    if (thread == NULL)
    {
        return;
    }

    /* Copy the allocator out before the control block itself goes away. */
    alloc = thread->alloc;

#ifdef _WIN32
    (void)WaitForSingleObject(thread->handle, INFINITE);
    (void)CloseHandle(thread->handle);
#else
    (void)pthread_join(thread->handle, NULL);
#endif

    ql_dealloc(&alloc, thread);
}

void ql_thread_detach(ql_thread_t* thread)
{
    ql_allocator_t alloc;

    if (thread == NULL)
    {
        return;
    }

    alloc = thread->alloc;

#ifdef _WIN32
    /* Closing the only handle to a running thread lets it finish unwatched;
     * the kernel releases its resources when the last handle closes. */
    (void)CloseHandle(thread->handle);
#else
    (void)pthread_detach(thread->handle);
#endif

    ql_dealloc(&alloc, thread);
}

unsigned ql_thread_hardware_concurrency(void)
{
#ifdef _WIN32
    SYSTEM_INFO info;

    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (unsigned)info.dwNumberOfProcessors : 1U;
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);

    return count > 0 ? (unsigned)count : 1U;
#endif
}

/* ---------------------------------------------------------------------------
 * Mutexes
 * ------------------------------------------------------------------------ */

ql_ret_t ql_mutex_create(const ql_allocator_t* alloc, ql_mutex_t** out)
{
    ql_mutex_t* mutex;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_mutex_create: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    mutex = ql_alloc(alloc, sizeof *mutex);
    if (mutex == NULL)
    {
        return QL_ERR_NOMEM;
    }

    mutex->alloc = alloc != NULL ? *alloc : ql_default_allocator();

#ifdef _WIN32
    InitializeCriticalSection(&mutex->storage);
#else
    if (pthread_mutex_init(&mutex->storage, NULL) != 0)
    {
        ql_dealloc(&mutex->alloc, mutex);
        ql_err_raise(QL_ERR_SYS, "ql_mutex_create: pthread_mutex_init failed");
        return QL_ERR_SYS;
    }
#endif

    *out = mutex;
    return QL_OK;
}

void ql_mutex_destroy(ql_mutex_t* mutex)
{
    ql_allocator_t alloc;

    if (mutex == NULL)
    {
        return;
    }

    alloc = mutex->alloc;

#ifdef _WIN32
    DeleteCriticalSection(&mutex->storage);
#else
    (void)pthread_mutex_destroy(&mutex->storage);
#endif

    ql_dealloc(&alloc, mutex);
}

void ql_mutex_lock(ql_mutex_t* mutex)
{
    if (mutex == NULL)
    {
        return;
    }

#ifdef _WIN32
    EnterCriticalSection(&mutex->storage);
#else
    (void)pthread_mutex_lock(&mutex->storage);
#endif
}

void ql_mutex_unlock(ql_mutex_t* mutex)
{
    if (mutex == NULL)
    {
        return;
    }

#ifdef _WIN32
    LeaveCriticalSection(&mutex->storage);
#else
    (void)pthread_mutex_unlock(&mutex->storage);
#endif
}

/* ---------------------------------------------------------------------------
 * Condition variables
 * ------------------------------------------------------------------------ */

ql_ret_t ql_cond_create(const ql_allocator_t* alloc, ql_cond_t** out)
{
    ql_cond_t* cond;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_cond_create: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    cond = ql_alloc(alloc, sizeof *cond);
    if (cond == NULL)
    {
        return QL_ERR_NOMEM;
    }

    cond->alloc = alloc != NULL ? *alloc : ql_default_allocator();

#ifdef _WIN32
    InitializeConditionVariable(&cond->storage);
#else
    if (pthread_cond_init(&cond->storage, NULL) != 0)
    {
        ql_dealloc(&cond->alloc, cond);
        ql_err_raise(QL_ERR_SYS, "ql_cond_create: pthread_cond_init failed");
        return QL_ERR_SYS;
    }
#endif

    *out = cond;
    return QL_OK;
}

void ql_cond_destroy(ql_cond_t* cond)
{
    ql_allocator_t alloc;

    if (cond == NULL)
    {
        return;
    }

    alloc = cond->alloc;

#ifdef _WIN32
    /* A CONDITION_VARIABLE holds no resources to release. */
#else
    (void)pthread_cond_destroy(&cond->storage);
#endif

    ql_dealloc(&alloc, cond);
}

ql_ret_t ql_cond_wait(ql_cond_t* cond, ql_mutex_t* mutex, int timeout_ms)
{
    if (cond == NULL || mutex == NULL || timeout_ms < 0)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_cond_wait: cond or mutex is NULL, or timeout is negative");
        return QL_ERR_INVALID_ARG;
    }

#ifdef _WIN32
    {
        DWORD limit = timeout_ms == 0 ? INFINITE : (DWORD)timeout_ms;

        if (!SleepConditionVariableCS(&cond->storage, &mutex->storage, limit))
        {
            if (GetLastError() == ERROR_TIMEOUT)
            {
                return QL_ERR_TIMEOUT;
            }
            ql_err_raise(QL_ERR_SYS, "ql_cond_wait: SleepConditionVariableCS failed");
            return QL_ERR_SYS;
        }
    }
#else
    if (timeout_ms == 0)
    {
        if (pthread_cond_wait(&cond->storage, &mutex->storage) != 0)
        {
            ql_err_raise(QL_ERR_SYS, "ql_cond_wait: pthread_cond_wait failed");
            return QL_ERR_SYS;
        }
    }
    else
    {
        /*
         * pthread_cond_timedwait takes an absolute deadline, and with a default
         * condition variable that deadline is on CLOCK_REALTIME. Building it
         * from the current time is therefore both necessary and correct; the
         * cost is that a clock jump can stretch or shorten the wait, which is
         * why a timed wait should always be inside a predicate loop.
         */
        struct timespec deadline;
        int rc;

        /*
         * timespec_get() rather than clock_gettime(CLOCK_REALTIME): the latter
         * is POSIX.1b and stays hidden under -std=c11, which is exactly the
         * dialect qlib builds in. C11's timespec_get(TIME_UTC) gives the same
         * wall clock that a default pthread_cond_timedwait measures against.
         */
        (void)timespec_get(&deadline, TIME_UTC);
        deadline.tv_sec += (time_t)(timeout_ms / 1000);
        deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }

        rc = pthread_cond_timedwait(&cond->storage, &mutex->storage, &deadline);
        if (rc == ETIMEDOUT)
        {
            return QL_ERR_TIMEOUT;
        }
        if (rc != 0)
        {
            ql_err_raise(QL_ERR_SYS, "ql_cond_wait: pthread_cond_timedwait failed");
            return QL_ERR_SYS;
        }
    }
#endif

    return QL_OK;
}

void ql_cond_signal(ql_cond_t* cond)
{
    if (cond == NULL)
    {
        return;
    }

#ifdef _WIN32
    WakeConditionVariable(&cond->storage);
#else
    (void)pthread_cond_signal(&cond->storage);
#endif
}

void ql_cond_broadcast(ql_cond_t* cond)
{
    if (cond == NULL)
    {
        return;
    }

#ifdef _WIN32
    WakeAllConditionVariable(&cond->storage);
#else
    (void)pthread_cond_broadcast(&cond->storage);
#endif
}
