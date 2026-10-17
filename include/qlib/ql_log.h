#ifndef QLIB_QL_LOG_H
#define QLIB_QL_LOG_H

/**
 * @file ql_log.h
 * @brief A small logger: levels, formatting, and console and rotating-file
 *        sinks.
 *
 * Three decisions shape this module and are worth stating up front.
 *
 * **It is a singleton.** Every other module in qlib is an opaque handle, and
 * this one is not: `ql_log_init()` sets up process-wide state and the macros
 * below use it. A logger is the one thing that is genuinely per-process rather
 * than per-object, and threading a handle through every call site would be felt
 * in every function that ever logs. It is the second piece of global state in
 * the library, after the error-handler registry, and ADR-18 records it rather
 * than hiding it.
 *
 * **It has fixed-size buffers.** A log record is bounded on purpose: a record
 * that needs an arbitrary amount of memory turns a diagnostic into an allocation
 * failure. `QL_LOG_LINE_MAX` is part of the format, not a tuning knob, and a
 * message that does not fit is truncated rather than dropped. This is the
 * opposite of qlib's usual "no exposed layout" rule and is the other thing
 * ADR-18 records.
 *
 * **It needs the threading module.** Writes are serialised by a `ql_mutex_t`,
 * because a logger shared between threads that interleaves half-lines is worse
 * than no logger. `QLIB_BUILD_LOG` therefore requires `QLIB_BUILD_THREADS`.
 *
 * There is no `#ifdef` in `src/ql_log.c`: everything it needs is C11. That is
 * why colour is explicit rather than sniffing for a terminal -- `isatty` and
 * `fileno` are POSIX, and staying out of that keeps the module portable for
 * free.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "qlib/ql_common.h"
#include "qlib/ql_mem.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Longest formatted record, terminator included.
 *
 * A message that does not fit is truncated, and the record is still emitted:
 * losing the tail of a long diagnostic is better than losing the diagnostic.
 */
#define QL_LOG_LINE_MAX 1024

/** @brief Ask the compiler to check printf-style arguments where it can. */
#if defined(__GNUC__) || defined(__clang__)
#  define QL_LOG_PRINTF(fmt_index, arg_index) __attribute__((format(printf, fmt_index, arg_index)))
#else
#  define QL_LOG_PRINTF(fmt_index, arg_index)
#endif

/*
 * The levels exist twice: once as macros, because #if cannot compare against an
 * enum constant, and once as the enum below, which is what the API takes. Keep
 * the two lists in step; the enum is defined in terms of the macros so a typo
 * cannot silently renumber anything.
 */
/** @brief Preprocessor value of level trace, for QLIB_LOG_COMPILE_LEVEL. */
#define QL_LOG_LEVEL_TRACE 0
/** @brief Preprocessor value of level debug, for QLIB_LOG_COMPILE_LEVEL. */
#define QL_LOG_LEVEL_DEBUG 1
/** @brief Preprocessor value of level info, for QLIB_LOG_COMPILE_LEVEL. */
#define QL_LOG_LEVEL_INFO 2
/** @brief Preprocessor value of level warn, for QLIB_LOG_COMPILE_LEVEL. */
#define QL_LOG_LEVEL_WARN 3
/** @brief Preprocessor value of level error, for QLIB_LOG_COMPILE_LEVEL. */
#define QL_LOG_LEVEL_ERROR 4
/** @brief Preprocessor value of level fatal, for QLIB_LOG_COMPILE_LEVEL. */
#define QL_LOG_LEVEL_FATAL 5
/** @brief Preprocessor value of level off, for QLIB_LOG_COMPILE_LEVEL. */
#define QL_LOG_LEVEL_OFF 6

/** @brief Severity, lowest to highest. */
typedef enum
{
    QL_LOG_TRACE = QL_LOG_LEVEL_TRACE, /**< Fine-grained tracing. */
    QL_LOG_DEBUG = QL_LOG_LEVEL_DEBUG, /**< Diagnostic detail. */
    QL_LOG_INFO = QL_LOG_LEVEL_INFO,   /**< Normal operation. */
    QL_LOG_WARN = QL_LOG_LEVEL_WARN,   /**< Something is off but recoverable. */
    QL_LOG_ERROR = QL_LOG_LEVEL_ERROR, /**< An operation failed. */
    QL_LOG_FATAL = QL_LOG_LEVEL_FATAL, /**< The process cannot continue. */
    QL_LOG_OFF = QL_LOG_LEVEL_OFF,     /**< Silence everything. */
} ql_log_level_t;

/** @brief How a record is rendered. */
typedef enum
{
    QL_LOG_FORMAT_PLAIN = 0, /**< `time level file:line message`. */
    QL_LOG_FORMAT_RAW,       /**< The message only. */
    QL_LOG_FORMAT_JSON,      /**< One JSON object per line. */
} ql_log_format_t;

/** @brief Console sink: writes to `stderr`. */
#define QL_LOG_SINK_CONSOLE 1U
/** @brief File sink: requires ql_log_set_file(). */
#define QL_LOG_SINK_FILE 2U

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Set up the logger.
 *
 * Idempotent. If it is never called, the first write sets it up with defaults:
 * level `QL_LOG_INFO`, console sink, `PLAIN` format, no colour, local time.
 *
 * @param alloc Allocator for the logger's own state, or NULL for the default.
 * @return QL_OK, or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_log_init(const ql_allocator_t* alloc);

/**
 * @brief Flush and release everything the logger holds.
 *
 * The next write re-initialises with defaults. Safe to call when not
 * initialised.
 */
QLIB_EXPORT void ql_log_shutdown(void);

/** @brief Flush the file sink. @return QL_OK, or QL_ERR_IO. */
QLIB_EXPORT ql_ret_t ql_log_flush(void);

/* ------------------------------------------------------------------------- */
/* Level                                                                      */
/* ------------------------------------------------------------------------- */

/**
 * @brief Set the lowest level that is emitted.
 *
 * Applies immediately, to every thread. `QL_LOG_OFF` silences everything.
 */
QLIB_EXPORT void ql_log_set_level(ql_log_level_t level);

/** @brief The current threshold. */
QLIB_NODISCARD QLIB_EXPORT ql_log_level_t ql_log_get_level(void);

/** @brief Whether a record at @p level would be emitted right now. */
QLIB_NODISCARD QLIB_EXPORT bool ql_log_enabled(ql_log_level_t level);

/** @brief The lowercase name of a level, or NULL for an out-of-range value. */
QLIB_NODISCARD QLIB_EXPORT const char* ql_log_level_name(ql_log_level_t level);

/**
 * @brief Parse a level name, case insensitively.
 *
 * Accepts the names returned by ql_log_level_name() plus a few obvious aliases:
 * "warning" for warn, "err" for error, "critical" for fatal.
 *
 * @return QL_OK, QL_ERR_NOT_FOUND for an unknown name, or QL_ERR_INVALID_ARG.
 */
QLIB_EXPORT ql_ret_t ql_log_level_from_name(const char* name, ql_log_level_t* out);

/**
 * @brief Set the level from an environment variable.
 *
 * Reads once and leaves the level alone when the variable is unset or holds an
 * unknown name, so `QLOG_LEVEL=debug ./app` works without the application
 * having to parse anything.
 *
 * @return QL_OK when the level was set, QL_ERR_NOT_FOUND when the variable is
 *         unset or unreadable, QL_ERR_INVALID_ARG for an unknown level name.
 */
QLIB_EXPORT ql_ret_t ql_log_set_level_from_env(const char* variable);

/* ------------------------------------------------------------------------- */
/* Format and sinks                                                           */
/* ------------------------------------------------------------------------- */

/** @brief Choose the rendering. Applies to records written after the call. */
QLIB_EXPORT void ql_log_set_format(ql_log_format_t format);

/**
 * @brief Replace the sink set with a bitmask of `QL_LOG_SINK_*`.
 *
 * The file sink only produces output once ql_log_set_file() has been given a
 * path; enabling it earlier is harmless and simply emits nothing.
 */
QLIB_EXPORT void ql_log_set_sinks(unsigned sinks);

/**
 * @brief Turn ANSI colour on or off for the console sink. Off by default.
 *
 * Explicit rather than automatic: detecting a terminal needs `isatty`, which is
 * POSIX, and this module has no platform code at all.
 */
QLIB_EXPORT void ql_log_set_color(bool on);

/** @brief Timestamps in UTC rather than local time. Local is the default. */
QLIB_EXPORT void ql_log_set_utc(bool on);

/* ------------------------------------------------------------------------- */
/* File sink                                                                  */
/* ------------------------------------------------------------------------- */

/**
 * @brief Send records to a file, creating or appending to it.
 *
 * Opens immediately, and enables the file sink. @p path is copied.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_NOMEM, or QL_ERR_IO if the file
 *         could not be opened. On failure any previously open file stays open.
 */
QLIB_EXPORT ql_ret_t ql_log_set_file(const char* path);

/**
 * @brief Rotate the log file when it grows past @p max_bytes.
 *
 * When the file would exceed the limit, it is renamed to `<path>.1`, earlier
 * backups are shifted down, and `<path>.<backups>` is deleted. @p max_bytes of 0
 * disables rotation, which is the default. @p backups below 1 becomes 1.
 */
QLIB_EXPORT void ql_log_set_rotation(size_t max_bytes, int backups);

/* ------------------------------------------------------------------------- */
/* Writing                                                                    */
/* ------------------------------------------------------------------------- */

/**
 * @brief Write a record.
 *
 * Prefer the macros below: they fill in @p file, @p line and @p func from the
 * call site. Calling this directly is only useful when wrapping the logger.
 *
 * @param level Severity; records below the threshold are dropped before the
 *              message is formatted.
 * @param file  Caller's file, usually `__FILE__`.
 * @param line  Caller's line, usually `__LINE__`.
 * @param func  Caller's function, usually `__func__`.
 * @param fmt   printf format. Must not be NULL.
 */
QLIB_EXPORT void ql_log_write(ql_log_level_t level, const char* file, int line, const char* func, const char* fmt, ...)
    QL_LOG_PRINTF(5, 6);

/** @brief ql_log_write() with a `va_list`. */
QLIB_EXPORT void ql_log_vwrite(ql_log_level_t level, const char* file, int line, const char* func, const char* fmt,
                               va_list ap);

/**
 * @brief The level below which calls compile out.
 *
 * Define `QLIB_LOG_COMPILE_LEVEL` to one of the `QL_LOG_LEVEL_*` values to drop
 * every call below it at compile time. `QL_LOG_LEVEL_TRACE` keeps everything.
 */
#ifndef QLIB_LOG_COMPILE_LEVEL
#  define QLIB_LOG_COMPILE_LEVEL QL_LOG_LEVEL_TRACE
#endif

#if QLIB_LOG_COMPILE_LEVEL <= QL_LOG_LEVEL_TRACE
/** @brief Emit a TRACE record with the call site filled in; compiled out above the floor. */
#  define QL_LOG_TRACE(...) ql_log_write(QL_LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
/** @brief Compiled out: QLIB_LOG_COMPILE_LEVEL is above trace. */
#  define QL_LOG_TRACE(...) ((void)0)
#endif

#if QLIB_LOG_COMPILE_LEVEL <= QL_LOG_LEVEL_DEBUG
/** @brief Emit a DEBUG record with the call site filled in; compiled out above the floor. */
#  define QL_LOG_DEBUG(...) ql_log_write(QL_LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
/** @brief Compiled out: QLIB_LOG_COMPILE_LEVEL is above debug. */
#  define QL_LOG_DEBUG(...) ((void)0)
#endif

#if QLIB_LOG_COMPILE_LEVEL <= QL_LOG_LEVEL_INFO
/** @brief Emit a INFO record with the call site filled in; compiled out above the floor. */
#  define QL_LOG_INFO(...) ql_log_write(QL_LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
/** @brief Compiled out: QLIB_LOG_COMPILE_LEVEL is above info. */
#  define QL_LOG_INFO(...) ((void)0)
#endif

#if QLIB_LOG_COMPILE_LEVEL <= QL_LOG_LEVEL_WARN
/** @brief Emit a WARN record with the call site filled in; compiled out above the floor. */
#  define QL_LOG_WARN(...) ql_log_write(QL_LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
/** @brief Compiled out: QLIB_LOG_COMPILE_LEVEL is above warn. */
#  define QL_LOG_WARN(...) ((void)0)
#endif

#if QLIB_LOG_COMPILE_LEVEL <= QL_LOG_LEVEL_ERROR
/** @brief Emit a ERROR record with the call site filled in; compiled out above the floor. */
#  define QL_LOG_ERROR(...) ql_log_write(QL_LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
/** @brief Compiled out: QLIB_LOG_COMPILE_LEVEL is above error. */
#  define QL_LOG_ERROR(...) ((void)0)
#endif

#if QLIB_LOG_COMPILE_LEVEL <= QL_LOG_LEVEL_FATAL
/** @brief Emit a FATAL record with the call site filled in; compiled out above the floor. */
#  define QL_LOG_FATAL(...) ql_log_write(QL_LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
/** @brief Compiled out: QLIB_LOG_COMPILE_LEVEL is above fatal. */
#  define QL_LOG_FATAL(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_LOG_H */
