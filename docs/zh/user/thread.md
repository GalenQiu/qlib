# 线程

`<qlib/ql_thread.h>` 提供三样东西：`ql_thread_t`、`ql_mutex_t` 与 `ql_cond_t`，
在 POSIX 上基于 `pthread`，在 Windows 上基于对应的 Win32 实现。

该模块之所以存在，是因为先前的两个决策反复撞上同一堵墙：`ql_net` 的 socket 是
阻塞的，因为非阻塞 I/O 需要线程或事件循环，而任何异步能力都无从谈起。它是
**可选**的（`QLIB_BUILD_THREADS`），因为它是链接依赖——本库的第二个链接依赖。

```bash
cmake -S . -B build -DQLIB_BUILD_THREADS=ON
```

## 为什么不用 C11 的 `<threads.h>`？

因为 macOS 不提供它。它是标准 C11 的一部分，而本库支持三个平台；三个平台中有一个
缺失的标准特性，对我们来说就是不可用的。替代方案——让每个调用方自己写
`#ifdef`——会让同样的二十行代码在每个调用方那里各出现一次，而且每一份都只由
写下它的人测试。

## 这三个类型是句柄，而不是值

```c
ql_thread_t* thread;
ql_mutex_t* mutex;
ql_cond_t* cond;
```

它们是指向不透明句柄的指针，因此一个 `ql_mutex_t` 的代价是一次分配。这是刻意的：
`pthread_mutex_t` 与 `CRITICAL_SECTION` 没有可移植的大小，因此一个可内嵌的
结构体要么在所有地方浪费内存，要么在保留空间太小的那个平台上静默破坏内存。

## 一个典型的工作线程

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
        ql_thread_join(threads[i]);   /* 等待结束，然后释放句柄 */
    }

    printf("counter = %d\n", shared.counter);
    ql_mutex_destroy(shared.mutex);
    return 0;
}
```

```
counter = 40000
```

去掉锁，总数就会不足，因为 `shared->counter++` 是一次读-改-写。该模块自己的
测试断言了精确的总数，正是出于这个原因。

## 条件变量

```c
ql_ret_t ql_cond_wait(ql_cond_t* cond, ql_mutex_t* mutex, int timeout_ms);
```

调用前必须已经持有互斥量；等待期间它会被释放，返回之前重新获取。
`timeout_ms` 为 0 表示永远等待。

| 返回 | 含义 |
|---|---|
| `QL_OK` | 收到信号 |
| `QL_ERR_TIMEOUT` | 等待超时 |
| `QL_ERR_INVALID_ARG` | `NULL` 参数，或负数超时 |

**始终在循环中测试谓词。** 条件变量可能发生虚假唤醒，而单次信号也可能被另一个
等待者消费掉：

```c
ql_mutex_lock(shared->mutex);
while (!shared->finished) {
    if (ql_cond_wait(shared->cond, shared->mutex, 5000) == QL_ERR_TIMEOUT) {
        break;   /* 超时不是信号 */
    }
}
ql_mutex_unlock(shared->mutex);
```

`ql_cond_signal()` 唤醒一个等待者，`ql_cond_broadcast()` 唤醒所有等待者。

## 等待结束（join）与分离（detach）

| 调用 | 效果 |
|---|---|
| `ql_thread_join(thread)` | 等待线程结束，然后释放句柄 |
| `ql_thread_detach(thread)` | 释放句柄，让线程在无人看管下结束 |

分离（detach）的线程无法被等待，因此没有办法知道它已经结束——只能知道它已经
完成了自己的工作。**它绝不能触碰调用方即将释放的任何东西。** 要由线程在自己
受同步保护的区段内发出完成信号，让调用方观察那个信号，而不是观察线程的退出。

这不是风格上的提醒：本模块的第一个版本让一个分离（detach）的线程去读取一个
`ql_thread_detach()` 已经释放的启动上下文，ASAN 抓住了它。现在上下文是一次
独立的分配，由线程自己拥有，并在其函数体返回时由线程自己释放。

## 该用多少个线程？

```c
unsigned n = ql_thread_hardware_concurrency();
```

这只是建议值，用于确定线程池的大小。平台无法给出时返回 1。它不是
`ql_thread_create()` 的上限。

## 代价与平台支持

| 项目 | 说明 |
|---|---|
| 链接依赖 | `Threads::Threads`（在现代 glibc 与 macOS 上为空操作） |
| 每个线程 | 两次分配：句柄与启动上下文 |
| 每个互斥量 / 条件变量 | 各一次分配 |
| Windows | `_beginthreadex`、`CRITICAL_SECTION`、`CONDITION_VARIABLE` —— **仅由 CI 验证** |
| POSIX | `pthread`；定时等待使用 C11 的 `timespec_get`，而不是 POSIX.1b 的 `clock_gettime`，后者在 `-std=c11` 下是隐藏的 |

## 错误码

| 情况 | 错误码 |
|---|---|
| `NULL` 输出参数或函数指针；负数超时 | `QL_ERR_INVALID_ARG` |
| 分配失败 | `QL_ERR_NOMEM` |
| 平台拒绝启动线程或初始化对象 | `QL_ERR_SYS` |
| 条件变量等待超时 | `QL_ERR_TIMEOUT` |

`QL_ERR_TIMEOUT` 是普通的回答而不是故障，因此它会被返回，但不会上报给
[错误回调](errors.md)。
