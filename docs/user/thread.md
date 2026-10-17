# Threads

`<qlib/ql_thread.h>` provides three things: `ql_thread_t`, `ql_mutex_t` and
`ql_cond_t`, over `pthread` on POSIX and the Win32 equivalents on Windows.

The module exists because two earlier decisions kept hitting the same wall:
`ql_net`'s sockets are blocking because non-blocking I/O needs a thread or an
event loop, and an asynchronous anything was impossible. It is **optional**
(`QLIB_BUILD_THREADS`), because it is a link dependency — the second one in the
library.

```bash
cmake -S . -B build -DQLIB_BUILD_THREADS=ON
```

## Why not C11 `<threads.h>`?

Because macOS does not ship it. It is standard C11, and this library supports
three platforms; a standard feature one of them omits is not available to us.
The alternative — every caller writing their own `#ifdef`s — would produce the
same twenty lines once per caller, each tested only by whoever wrote it.

## The three types are handles, not values

```c
ql_thread_t* thread;
ql_mutex_t* mutex;
ql_cond_t* cond;
```

They are opaque pointers, so a `ql_mutex_t` costs one allocation. That is
deliberate: `pthread_mutex_t` and `CRITICAL_SECTION` have no portable size, so an
embeddable struct would either waste memory everywhere or silently corrupt memory
on the one platform where the reservation is too small.

## A typical worker

```c
#include <qlib/ql_thread.h>
#include <stdio.h>

typedef struct {
    ql_mutex_t* mutex;
    int counter;
} shared_t;

static void worker(void* arg)
{
    shared_t* shared = arg;
    int i;

    for (i = 0; i < 10000; i++) {
        ql_mutex_lock(shared->mutex);
        shared->counter++;
        ql_mutex_unlock(shared->mutex);
    }
}

int main(void)
{
    shared_t shared = {NULL, 0};
    ql_thread_t* threads[4];
    int i;

    if (ql_mutex_create(NULL, &shared.mutex) != QL_OK) {
        return 1;
    }
    for (i = 0; i < 4; i++) {
        if (ql_thread_create(NULL, worker, &shared, &threads[i]) != QL_OK) {
            return 1;
        }
    }
    for (i = 0; i < 4; i++) {
        ql_thread_join(threads[i]);   /* waits, then frees the handle */
    }

    printf("counter = %d\n", shared.counter);
    ql_mutex_destroy(shared.mutex);
    return 0;
}
```

```
counter = 40000
```

Remove the lock and the total falls short, because `shared->counter++` is a
read-modify-write. The module's own test asserts the exact total for exactly this
reason.

## Condition variables

```c
ql_ret_t ql_cond_wait(ql_cond_t* cond, ql_mutex_t* mutex, int timeout_ms);
```

The mutex must already be held; it is released while waiting and re-acquired
before returning. `timeout_ms` of 0 waits forever.

| Return | Meaning |
|---|---|
| `QL_OK` | Signalled |
| `QL_ERR_TIMEOUT` | The wait expired |
| `QL_ERR_INVALID_ARG` | A NULL argument, or a negative timeout |

**Always test the predicate in a loop.** A condition variable may wake
spuriously, and a single signal may be consumed by another waiter:

```c
ql_mutex_lock(shared->mutex);
while (!shared->finished) {
    if (ql_cond_wait(shared->cond, shared->mutex, 5000) == QL_ERR_TIMEOUT) {
        break;   /* a timeout is not a signal */
    }
}
ql_mutex_unlock(shared->mutex);
```

`ql_cond_signal()` wakes one waiter, `ql_cond_broadcast()` wakes all of them.

## Joining and detaching

| Call | Effect |
|---|---|
| `ql_thread_join(thread)` | Wait for the thread to finish, then free the handle |
| `ql_thread_detach(thread)` | Free the handle and let the thread finish unwatched |

A detached thread cannot be waited for, so there is no way to know it has
finished — only that it has done its work. **It must not touch anything the
caller is about to free.** Signal completion from inside the thread's own
synchronised section, and have the caller observe that, not the thread's exit.

This is not a style note: the first version of this module let a detached thread
read a start context that `ql_thread_detach()` had already freed, and ASAN caught
it. The context is now a separate allocation owned by the thread, freed by the
thread itself when its body returns.

## How many threads?

```c
unsigned n = ql_thread_hardware_concurrency();
```

Advisory, for sizing a pool. Returns 1 when the platform cannot say. It is not a
limit on `ql_thread_create()`.

## Cost and platform support

| Item | Note |
|---|---|
| Link dependency | `Threads::Threads` (a no-op on modern glibc and macOS) |
| Per thread | Two allocations: the handle and the start context |
| Per mutex / condition | One allocation each |
| Windows | `_beginthreadex`, `CRITICAL_SECTION`, `CONDITION_VARIABLE` — **verified by CI only** |
| POSIX | `pthread`; the timed wait uses C11 `timespec_get` rather than POSIX.1b `clock_gettime`, which stays hidden under `-std=c11` |

## Error codes

| Situation | Code |
|---|---|
| `NULL` output or function pointer; negative timeout | `QL_ERR_INVALID_ARG` |
| Allocation failed | `QL_ERR_NOMEM` |
| The platform refused to start a thread or initialise an object | `QL_ERR_SYS` |
| A condition-variable wait expired | `QL_ERR_TIMEOUT` |

`QL_ERR_TIMEOUT` is an ordinary answer rather than a fault, so it is returned but
not reported to the [error handler](errors.md).
