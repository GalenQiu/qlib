# 日志

`<qlib/ql_log.h>` 提供级别、格式化，以及控制台与轮转文件两种输出目标（sink）。
该模块是可选的，并且需要线程模块：

```bash
cmake -S . -B build -DQLIB_BUILD_THREADS=ON -DQLIB_BUILD_LOG=ON
```

`QLIB_BUILD_LOG` 在没有 `QLIB_BUILD_THREADS` 时是配置错误，而不是警告，因为
日志器用互斥量串行化写入，而一个会在各线程之间交错输出半行的日志器，比没有
日志器更糟。

## 塑造它的两个事实

**它是一个单例。** qlib 中的其他每个模块都是不透明句柄；这一个则是由
`ql_log_init()` 建立的进程级状态。日志器是唯一真正属于每进程的东西，而把一个
句柄穿过每个调用点，会让每一个曾经打过日志的函数都感受到这种负担。

**日志记录是有界的。** 一条需要任意大小内存的记录，会把一次诊断变成一次分配
失败。`QL_LOG_LINE_MAX`（1024 字节）是格式的一部分，而不是可调参数；放不下的
消息会被**截断，而不是丢弃**，因为丢掉一段长诊断的尾部，好过丢掉整条诊断。

两者都偏离了库的其余部分，[ADR-18](../technical/design-decisions.md) 记录了它们。

## 写入记录

```c
#include <qlib/ql_log.h>

if (ql_log_init(NULL) != QL_OK) {
    return 1;
}
ql_log_set_level(QL_LOG_INFO);

QL_LOG_INFO("starting up, pid %d", 4242);
QL_LOG_WARN("cache is %d%% full", 91);
QL_LOG_ERROR("could not open %s", "config.toml");
```

以 PLAIN 格式发送到文件，会得到：

```
2026-10-17 21:33:22 INFO  demo.c:12 starting up, pid 4242
2026-10-17 21:33:22 WARN  demo.c:13 cache is 91% full
2026-10-17 21:33:22 ERROR demo.c:14 could not open config.toml
```

`QL_LOG_TRACE` 到 `QL_LOG_FATAL` 都是宏，因此文件名、行号和函数名都来自调用点。
它们也是获得编译期剥离的唯一途径：

```bash
cc -DQLIB_LOG_COMPILE_LEVEL=QL_LOG_LEVEL_WARN app.c
```

这会把每个 `QL_LOG_INFO` 及更低级别变成 `((void)0)`——没有格式化、没有调用、
没有分支。`ql_log_write()` 仍然存在，供包装函数使用，它们改为付出运行时检查的
代价。

## 级别与过滤

```c
ql_log_set_level(QL_LOG_WARN);        /* 运行时阈值 */
if (ql_log_enabled(QL_LOG_DEBUG)) {   /* 避免构造昂贵的消息 */
    ...
}
```

或者从环境变量读取，这样 `QLOG_LEVEL=debug ./app` 无需应用自己解析任何东西
即可工作：

```c
(void)ql_log_set_level_from_env("QLOG_LEVEL");
```

当该变量未设置时，它返回 `QL_ERR_NOT_FOUND`，并且不改变级别——未配置的环境是
无害的。

`ql_log_level_from_name()` 不区分大小写，并接受几个人们真的会输入的名字：
`warning`、`err`、`critical`。

## 格式

| 格式 | 输出 |
|---|---|
| `QL_LOG_FORMAT_PLAIN` | `time LEVEL file:line message` |
| `QL_LOG_FORMAT_RAW` | 仅消息本身，便于管道处理 |
| `QL_LOG_FORMAT_JSON` | 每行一个对象 |

```c
ql_log_set_format(QL_LOG_FORMAT_JSON);
QL_LOG_ERROR("could not open \"config.toml\"");
```

```json
{"level":"error","file":"app.c","line":10,"func":"main","msg":"could not open \"config.toml\""}
```

引号、反斜杠、换行符和控制字符都会被转义，因此一条消息绝不会产生一行不是
合法 JSON 对象的内容。

颜色是**显式的**，而不是自动的：

```c
ql_log_set_color(true);
```

检测终端需要 `isatty`，而它是 POSIX 的。`src/ql_log.c` 中完全没有 `#ifdef`——
这正是颜色要由调用方决定的原因。

## 输出目标（sink）与轮转

```c
ql_log_set_file("app.log");                     /* 同时启用文件输出目标 */
ql_log_set_sinks(QL_LOG_SINK_FILE);             /* 或 CONSOLE，或两者 */
ql_log_set_rotation(4 * 1024 * 1024, 3);        /* 4 MiB，保留三个备份 */
```

当文件即将超过上限时，它会变成 `app.log.1`，更早的备份依次后移，而
`app.log.<backups>` 会被删除。

`ql_log_flush()` 立即把文件输出写出去。普通记录是缓冲的，因此一次崩溃可能丢掉
最后几行；`QL_LOG_FATAL` 会自行刷新（flush），因为它是你最希望能在崩溃后留存
下来的那条记录。

## 关闭

```c
ql_log_shutdown();   /* 刷新（flush）、关闭、释放；下一次写入会重新初始化 */
```

调用两次 `ql_log_init()` 没有问题，关闭两次也没有问题。

## 代价

| 项目 | 说明 |
|---|---|
| 链接依赖 | 线程模块（及其 `Threads::Threads`） |
| 每条记录 | 每个输出目标一次 `vsnprintf` 加一次 `fwrite` |
| 同步 | 每条记录一把互斥量；整条记录都在持锁期间写入 |
| 编译期剥离 | `QLIB_LOG_COMPILE_LEVEL`，运行时零开销 |

## 错误码

| 情况 | 错误码 |
|---|---|
| `NULL` 输出参数或变量名 | `QL_ERR_INVALID_ARG` |
| 分配失败 | `QL_ERR_NOMEM` |
| 日志文件无法打开，或刷新失败 | `QL_ERR_IO` |
| `ql_log_level_from_name()` 不认识该名称 | `QL_ERR_NOT_FOUND` |
| `ql_log_set_level_from_env()` 没有找到变量 | `QL_ERR_NOT_FOUND` |
