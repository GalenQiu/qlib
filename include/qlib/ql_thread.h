#ifndef QLIB_QL_THREAD_H
#define QLIB_QL_THREAD_H

/**
 * @file ql_thread.h
 * @brief Threads, mutexes and condition variables with one API for POSIX and
 *        Windows.
 *
 * This module exists because two previous decisions kept running into the same
 * missing piece: `ql_net`'s sockets are blocking, and there is no asynchronous
 * anything, both because qlib had no way to start a thread. ADR-17 records why
 * the answer is a small portability layer rather than either of the alternatives
 * (requiring C11 `<threads.h>`, which macOS does not have, or leaving every
 * caller to write the `#ifdef`s themselves).
 *
 * All three types are opaque handles rather than embeddable values. That is a
 * deliberate cost: a `pthread_mutex_t` and a `CRITICAL_SECTION` do not have
 * portable sizes, so an embeddable struct would either reserve too much or
 * silently corrupt memory on a platform where the guess is too small. A handle
 * costs one allocation and moves that risk into the library.
 *
 * The module is optional (`QLIB_BUILD_THREADS`) because it is a link
 * dependency: `Threads::Threads`, which is a no-op on modern glibc and macOS but
 * real elsewhere.
 */

#include <stdbool.h>
#include <stddef.h>

#include "qlib/ql_common.h"
#include "qlib/ql_mem.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Storage-class specifier for thread-local objects.
 *
 * `_Thread_local` on anything with a C11 compiler; `__declspec(thread)` on
 * MSVC, which supports the standard spelling only from recent versions.
 *
 * One limitation is worth knowing before relying on it: a `__declspec(thread)`
 * object in a DLL loaded with `LoadLibrary()` at run time may not be
 * initialised. That is a documented MSVC restriction, not a bug here, and it is
 * why the only current user is the logger's per-thread field buffer -- where
 * failing loudly would be better than silently sharing one buffer between
 * threads.
 */
#ifdef _MSC_VER
#  define QL_THREAD_LOCAL __declspec(thread)
#else
#  define QL_THREAD_LOCAL _Thread_local
#endif

/** @brief An opaque thread. */
typedef struct ql_thread ql_thread_t;

/** @brief An opaque mutex. */
typedef struct ql_mutex ql_mutex_t;

/** @brief An opaque condition variable. */
typedef struct ql_cond ql_cond_t;

/**
 * @brief The function a thread runs.
 *
 * Returning from it ends the thread. @p arg is whatever was passed to
 * ql_thread_create().
 */
typedef void (*ql_thread_fn)(void* arg);

/**
 * @brief Create a mutex.
 *
 * @param alloc Allocator, or NULL for the default one.
 * @param out   Receives the mutex. Set to NULL before any failure.
 * @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_mutex_create(const ql_allocator_t* alloc, ql_mutex_t** out);

/**
 * @brief Destroy a mutex. Passing NULL is a no-op.
 *
 * Destroying a locked mutex, or one another thread is waiting on, is undefined.
 */
QLIB_EXPORT void ql_mutex_destroy(ql_mutex_t* mutex);

/**
 * @brief Lock a mutex, blocking until it is available.
 *
 * Passing NULL is a no-op, so a lock can follow a failed create.
 */
QLIB_EXPORT void ql_mutex_lock(ql_mutex_t* mutex);

/** @brief Unlock a mutex. Unlocking one this thread does not hold is undefined. */
QLIB_EXPORT void ql_mutex_unlock(ql_mutex_t* mutex);

/**
 * @brief Create a condition variable.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_cond_create(const ql_allocator_t* alloc, ql_cond_t** out);

/** @brief Destroy a condition variable. Passing NULL is a no-op. */
QLIB_EXPORT void ql_cond_destroy(ql_cond_t* cond);

/**
 * @brief Wait on a condition variable, releasing @p mutex while waiting.
 *
 * @param cond       Condition variable.
 * @param mutex      Mutex the caller already holds; re-acquired before return.
 * @param timeout_ms Milliseconds to wait, or 0 to wait indefinitely.
 * @return QL_OK when signalled, QL_ERR_TIMEOUT when the wait expired,
 *         QL_ERR_INVALID_ARG for a NULL argument.
 *
 * The usual rule applies: test the predicate in a loop around this call, because
 * a condition variable may wake spuriously and a signal may be consumed by
 * another waiter.
 */
QLIB_EXPORT ql_ret_t ql_cond_wait(ql_cond_t* cond, ql_mutex_t* mutex, int timeout_ms);

/** @brief Wake one thread waiting on @p cond. */
QLIB_EXPORT void ql_cond_signal(ql_cond_t* cond);

/** @brief Wake every thread waiting on @p cond. */
QLIB_EXPORT void ql_cond_broadcast(ql_cond_t* cond);

/**
 * @brief Start a thread.
 *
 * @param alloc Allocator, or NULL for the default one.
 * @param fn    Function to run. Must not be NULL.
 * @param arg   Passed to @p fn unchanged. May be NULL.
 * @param out   Receives the thread handle. Set to NULL before any failure.
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_NOMEM, or QL_ERR_SYS when the
 *         platform refused to start a thread.
 *
 * The new thread starts running before this returns; there is no "start later".
 */
QLIB_EXPORT ql_ret_t ql_thread_create(const ql_allocator_t* alloc, ql_thread_fn fn, void* arg, ql_thread_t** out);

/**
 * @brief Wait for a thread to finish, then free its handle.
 *
 * Passing NULL is a no-op. Joining the same thread twice, or joining one that
 * was detached, is undefined.
 */
QLIB_EXPORT void ql_thread_join(ql_thread_t* thread);

/**
 * @brief Let a thread run to completion unwatched, and free its handle.
 *
 * The thread's resources are released when it exits. Passing NULL is a no-op.
 */
QLIB_EXPORT void ql_thread_detach(ql_thread_t* thread);

/**
 * @brief How many threads this process can run at once.
 *
 * Advisory: a hint for sizing worker pools, not a limit on ql_thread_create().
 * Returns 1 when the platform cannot say.
 */
QLIB_NODISCARD QLIB_EXPORT unsigned ql_thread_hardware_concurrency(void);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_THREAD_H */
