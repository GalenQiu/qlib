# Logging

`<qlib/ql_log.h>` provides levels, formatting, and console and rotating-file
sinks. It is optional and it needs the threading module:

```bash
cmake -S . -B build -DQLIB_BUILD_THREADS=ON -DQLIB_BUILD_LOG=ON
```

`QLIB_BUILD_LOG` without `QLIB_BUILD_THREADS` is a configure error rather than a
warning, because the logger serialises writes with a mutex and a logger that
interleaves half-lines between threads is worse than no logger.

## The two facts that shape it

**It is a singleton.** Every other module in qlib is an opaque handle; this one
is process-wide state set up by `ql_log_init()`. A logger is the one thing that
is genuinely per-process, and threading a handle through every call site would
be felt in every function that ever logs.

**Log records are bounded.** A record that needs an arbitrary amount of memory
turns a diagnostic into an allocation failure. `QL_LOG_LINE_MAX` (1024 bytes) is
part of the format, not a tuning knob; a message that does not fit is
**truncated, not dropped**, because losing the tail of a long diagnostic beats
losing the diagnostic.

Both are departures from the rest of the library, and
[ADR-18](../technical/design-decisions.md) records them.

## Writing records

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

Sent to a file with the plain format, that produces:

```
2026-10-17 21:33:22 INFO  demo.c:12 starting up, pid 4242
2026-10-17 21:33:22 WARN  demo.c:13 cache is 91% full
2026-10-17 21:33:22 ERROR demo.c:14 could not open config.toml
```

`QL_LOG_TRACE` through `QL_LOG_FATAL` are macros so that the file, line and
function come from the call site. They are also the only way to get compile-time
stripping:

```bash
cc -DQLIB_LOG_COMPILE_LEVEL=QL_LOG_LEVEL_WARN app.c
```

That turns every `QL_LOG_INFO` and below into `((void)0)` — no formatting, no
call, no branch. `ql_log_write()` still exists for wrappers, which pay the
runtime check instead.

## Levels and filtering

```c
ql_log_set_level(QL_LOG_WARN);        /* runtime threshold */
if (ql_log_enabled(QL_LOG_DEBUG)) {   /* avoid building an expensive message */
    ...
}
```

Or from the environment, so `QLOG_LEVEL=debug ./app` works without the
application parsing anything:

```c
(void)ql_log_set_level_from_env("QLOG_LEVEL");
```

It returns `QL_ERR_NOT_FOUND` when the variable is unset, and leaves the level
alone — an unconfigured environment is harmless.

`ql_log_level_from_name()` is case insensitive and accepts a few names people
actually type: `warning`, `err`, `critical`.

## Formats

| Format | Output |
|---|---|
| `QL_LOG_FORMAT_PLAIN` | `time LEVEL file:line message` |
| `QL_LOG_FORMAT_RAW` | The message only, for piping |
| `QL_LOG_FORMAT_JSON` | One object per line |

```c
ql_log_set_format(QL_LOG_FORMAT_JSON);
QL_LOG_ERROR("could not open \"config.toml\"");
```

```json
{"level":"error","file":"app.c","line":10,"func":"main","msg":"could not open \"config.toml\""}
```

Quotes, backslashes, newlines and control characters are escaped, so a message
can never produce a line that is not a valid JSON object.

Colour is **explicit**, not automatic:

```c
ql_log_set_color(true);
```

Detecting a terminal needs `isatty`, which is POSIX. `src/ql_log.c` contains no
`#ifdef` at all — that is why colour is a decision the caller makes.

## Sinks and rotation

```c
ql_log_set_file("app.log");                     /* also enables the file sink */
ql_log_set_sinks(QL_LOG_SINK_FILE);             /* or CONSOLE, or both */
ql_log_set_rotation(4 * 1024 * 1024, 3);        /* 4 MiB, keep three backups */
```

When the file would exceed the limit it becomes `app.log.1`, earlier backups
shift down, and `app.log.<backups>` is deleted.

Files are opened in binary mode, so a record is written as bytes and the line
endings are `\n` on every platform. JSON lines in particular are parsed by
machines, and being identical everywhere is worth more than the local
convention.

`ql_log_flush()` writes file output out immediately. Ordinary records are
buffered, so a crash may lose the last few lines; `QL_LOG_FATAL` flushes on its
own, since it is the record you most want to survive.

## Shutdown

```c
ql_log_shutdown();   /* flush, close, release; the next write re-initialises */
```

Calling `ql_log_init()` twice is fine, and so is shutting down twice.

## Cost

| Item | Note |
|---|---|
| Link dependency | The threading module (and its `Threads::Threads`) |
| Per record | One `vsnprintf` plus one `fwrite` per sink |
| Synchronisation | One mutex per record; the whole record is written under it |
| Compile-time stripping | `QLIB_LOG_COMPILE_LEVEL`, free at runtime |

## Error codes

| Situation | Code |
|---|---|
| `NULL` output or variable name | `QL_ERR_INVALID_ARG` |
| Allocation failed | `QL_ERR_NOMEM` |
| The log file could not be opened, or the flush failed | `QL_ERR_IO` |
| `ql_log_level_from_name()` did not recognise the name | `QL_ERR_NOT_FOUND` |
| `ql_log_set_level_from_env()` found no variable | `QL_ERR_NOT_FOUND` |
