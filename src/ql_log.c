#include "qlib/ql_log.h"

#include "ql_internal.h"

/* QLIB_BUILD_LOG requires QLIB_BUILD_THREADS, which CMake enforces. */
#include "qlib/ql_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * The logger's whole state, in one place because it is a singleton.
 *
 * No #ifdef in this file: everything below is C89 or C11. That is why colour is
 * explicit instead of sniffing for a terminal (isatty and fileno are POSIX) and
 * why the timestamp uses localtime()/gmtime() rather than localtime_r() --
 * those are serialised by the logger's own mutex, so the static buffer they
 * return is never shared with another thread of ours.
 */
typedef struct
{
    ql_allocator_t alloc;
    ql_mutex_t* mutex;
    ql_log_level_t level;
    ql_log_format_t format;
    unsigned sinks;
    bool color;
    bool utc;
    FILE* file;
    char* path;
    size_t rotation_bytes;
    int rotation_backups;
    size_t written;
} ql_log_state;

static ql_log_state g_log;
static bool g_ready;

static const char* const g_level_names[] = {"trace", "debug", "info", "warn", "error", "fatal", "off"};
static const char* const g_level_labels[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"};

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/** @brief Case-insensitive comparison of a whole string, without strcasecmp. */
static bool ql_log_equals_ci(const char* a, const char* b)
{
    size_t i;

    for (i = 0;; i++)
    {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];

        if (ca >= 'A' && ca <= 'Z')
        {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z')
        {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb)
        {
            return false;
        }
        if (ca == '\0')
        {
            return true;
        }
    }
}

/** @brief The part of a path after the last separator. */
static const char* ql_log_basename(const char* path)
{
    const char* base = path;
    const char* p;

    for (p = path; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            base = p + 1;
        }
    }
    return base;
}

/**
 * @brief Append @p text to @p out as a JSON string body.
 *
 * Escapes what JSON requires and replaces control characters, so a log message
 * containing a quote or a newline cannot produce a line that is not a JSON
 * object.
 */
static void ql_log_json_escape(char* out, size_t cap, const char* text)
{
    size_t used = 0;
    size_t i;

    for (i = 0; text[i] != '\0' && used + 7 < cap; i++)
    {
        unsigned char c = (unsigned char)text[i];

        if (c == '"' || c == '\\')
        {
            out[used++] = '\\';
            out[used++] = (char)c;
        }
        else if (c == '\n')
        {
            out[used++] = '\\';
            out[used++] = 'n';
        }
        else if (c == '\r')
        {
            out[used++] = '\\';
            out[used++] = 'r';
        }
        else if (c == '\t')
        {
            out[used++] = '\\';
            out[used++] = 't';
        }
        else if (c < 0x20)
        {
            static const char hex[] = "0123456789abcdef";

            out[used++] = '\\';
            out[used++] = 'u';
            out[used++] = '0';
            out[used++] = '0';
            out[used++] = hex[((unsigned)c >> 4U) & 0x0FU];
            out[used++] = hex[(unsigned)c & 0x0FU];
        }
        else
        {
            out[used++] = (char)c;
        }
    }

    out[used] = '\0';
}

/** @brief Format the timestamp into @p out. */
static void ql_log_timestamp(char* out, size_t cap)
{
    time_t now = time(NULL);
    struct tm* parts = g_log.utc ? gmtime(&now) : localtime(&now);

    if (parts == NULL || strftime(out, cap, "%Y-%m-%d %H:%M:%S", parts) == 0)
    {
        out[0] = '\0';
    }
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------ */

ql_ret_t ql_log_init(const ql_allocator_t* alloc)
{
    ql_mutex_t* mutex = NULL;

    if (g_ready)
    {
        return QL_OK;
    }

    if (ql_mutex_create(alloc, &mutex) != QL_OK)
    {
        return QL_ERR_NOMEM;
    }

    g_log.alloc = alloc != NULL ? *alloc : ql_default_allocator();
    g_log.mutex = mutex;
    g_log.level = QL_LOG_INFO;
    g_log.format = QL_LOG_FORMAT_PLAIN;
    g_log.sinks = QL_LOG_SINK_CONSOLE;
    g_log.color = false;
    g_log.utc = false;
    g_log.file = NULL;
    g_log.path = NULL;
    g_log.rotation_bytes = 0;
    g_log.rotation_backups = 1;
    g_log.written = 0;
    g_ready = true;

    return QL_OK;
}

/** @brief Initialise on first use, so callers never have to. */
static bool ql_log_ensure(void)
{
    if (g_ready)
    {
        return true;
    }
    return ql_log_init(NULL) == QL_OK;
}

void ql_log_shutdown(void)
{
    ql_allocator_t alloc;

    if (!g_ready)
    {
        return;
    }

    if (g_log.file != NULL)
    {
        (void)fclose(g_log.file);
        g_log.file = NULL;
    }

    alloc = g_log.alloc;
    ql_dealloc(&alloc, g_log.path);
    g_log.path = NULL;

    ql_mutex_destroy(g_log.mutex);
    g_log.mutex = NULL;
    g_ready = false;
}

ql_ret_t ql_log_flush(void)
{
    if (!g_ready || g_log.file == NULL)
    {
        return QL_OK;
    }

    ql_mutex_lock(g_log.mutex);
    if (fflush(g_log.file) != 0)
    {
        ql_mutex_unlock(g_log.mutex);
        ql_err_raise(QL_ERR_IO, "ql_log_flush: fflush failed");
        return QL_ERR_IO;
    }
    ql_mutex_unlock(g_log.mutex);

    return QL_OK;
}

/* ---------------------------------------------------------------------------
 * Level
 * ------------------------------------------------------------------------ */

void ql_log_set_level(ql_log_level_t level)
{
    if (!ql_log_ensure())
    {
        return;
    }

    ql_mutex_lock(g_log.mutex);
    g_log.level = level;
    ql_mutex_unlock(g_log.mutex);
}

ql_log_level_t ql_log_get_level(void)
{
    return g_ready ? g_log.level : QL_LOG_INFO;
}

bool ql_log_enabled(ql_log_level_t level)
{
    if (!g_ready)
    {
        return level >= QL_LOG_INFO;
    }
    return level >= g_log.level && level != QL_LOG_OFF && g_log.level != QL_LOG_OFF;
}

const char* ql_log_level_name(ql_log_level_t level)
{
    int index = (int)level;

    if (index < QL_LOG_TRACE || index > QL_LOG_OFF)
    {
        return NULL;
    }
    return g_level_names[index];
}

ql_ret_t ql_log_level_from_name(const char* name, ql_log_level_t* out)
{
    int i;

    if (name == NULL || out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_log_level_from_name: name or out is NULL");
        return QL_ERR_INVALID_ARG;
    }

    for (i = QL_LOG_TRACE; i <= QL_LOG_OFF; i++)
    {
        if (ql_log_equals_ci(name, g_level_names[i]))
        {
            *out = (ql_log_level_t)i;
            return QL_OK;
        }
    }

    /* A few names people actually type. */
    if (ql_log_equals_ci(name, "warning"))
    {
        *out = QL_LOG_WARN;
        return QL_OK;
    }
    if (ql_log_equals_ci(name, "err"))
    {
        *out = QL_LOG_ERROR;
        return QL_OK;
    }
    if (ql_log_equals_ci(name, "critical") || ql_log_equals_ci(name, "crit"))
    {
        *out = QL_LOG_FATAL;
        return QL_OK;
    }

    return QL_ERR_NOT_FOUND;
}

ql_ret_t ql_log_set_level_from_env(const char* variable)
{
    const char* value;
    ql_log_level_t level;

    if (variable == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_log_set_level_from_env: variable is NULL");
        return QL_ERR_INVALID_ARG;
    }

    value = getenv(variable);
    if (value == NULL || value[0] == '\0')
    {
        return QL_ERR_NOT_FOUND;
    }

    if (ql_log_level_from_name(value, &level) != QL_OK)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_log_set_level_from_env: unknown level name");
        return QL_ERR_INVALID_ARG;
    }

    ql_log_set_level(level);
    return QL_OK;
}

/* ---------------------------------------------------------------------------
 * Format and sinks
 * ------------------------------------------------------------------------ */

void ql_log_set_format(ql_log_format_t format)
{
    if (!ql_log_ensure())
    {
        return;
    }

    ql_mutex_lock(g_log.mutex);
    g_log.format = format;
    ql_mutex_unlock(g_log.mutex);
}

void ql_log_set_sinks(unsigned sinks)
{
    if (!ql_log_ensure())
    {
        return;
    }

    ql_mutex_lock(g_log.mutex);
    g_log.sinks = sinks;
    ql_mutex_unlock(g_log.mutex);
}

void ql_log_set_color(bool on)
{
    if (!ql_log_ensure())
    {
        return;
    }

    ql_mutex_lock(g_log.mutex);
    g_log.color = on;
    ql_mutex_unlock(g_log.mutex);
}

void ql_log_set_utc(bool on)
{
    if (!ql_log_ensure())
    {
        return;
    }

    ql_mutex_lock(g_log.mutex);
    g_log.utc = on;
    ql_mutex_unlock(g_log.mutex);
}

/* ---------------------------------------------------------------------------
 * File sink
 * ------------------------------------------------------------------------ */

/**
 * @brief Shift backups down and start a fresh file.
 *
 * `<path>.N` is dropped, `<path>.N-1` becomes `<path>.N`, and the current file
 * becomes `<path>.1`.
 */
static void ql_log_rotate(void)
{
    char from[512];
    char to[512];
    int index;

    if (g_log.file != NULL)
    {
        (void)fclose(g_log.file);
        g_log.file = NULL;
    }

    for (index = g_log.rotation_backups - 1; index >= 1; index--)
    {
        (void)snprintf(from, sizeof from, "%s.%d", g_log.path, index);
        (void)snprintf(to, sizeof to, "%s.%d", g_log.path, index + 1);
        (void)rename(from, to);
    }

    (void)snprintf(to, sizeof to, "%s.1", g_log.path);
    (void)rename(g_log.path, to);

    g_log.file = fopen(g_log.path, "ab");
    g_log.written = 0;
}

ql_ret_t ql_log_set_file(const char* path)
{
    FILE* file;
    char* copy = NULL;

    if (!ql_log_ensure())
    {
        return QL_ERR_NOMEM;
    }

    if (path != NULL)
    {
        size_t length = strlen(path);

        copy = ql_alloc(&g_log.alloc, length + 1);
        if (copy == NULL)
        {
            return QL_ERR_NOMEM;
        }
        memcpy(copy, path, length + 1);

        /* Binary mode: a record is written as bytes, so the file does not
         * grow \r\n line endings on Windows. JSON lines in particular are
         * parsed by machine, and being identical on every platform is worth
         * more than the platform's convention. */
        file = fopen(path, "ab");
        if (file == NULL)
        {
            ql_dealloc(&g_log.alloc, copy);
            ql_err_raise(QL_ERR_IO, "ql_log_set_file: could not open the file");
            return QL_ERR_IO;
        }
    }
    else
    {
        file = NULL;
    }

    ql_mutex_lock(g_log.mutex);

    if (g_log.file != NULL)
    {
        (void)fclose(g_log.file);
    }
    ql_dealloc(&g_log.alloc, g_log.path);

    g_log.path = copy;
    g_log.file = file;
    g_log.written = 0;
    if (file != NULL)
    {
        g_log.sinks |= QL_LOG_SINK_FILE;
    }
    else
    {
        g_log.sinks &= ~QL_LOG_SINK_FILE;
    }

    ql_mutex_unlock(g_log.mutex);
    return QL_OK;
}

void ql_log_set_rotation(size_t max_bytes, int backups)
{
    if (!ql_log_ensure())
    {
        return;
    }

    ql_mutex_lock(g_log.mutex);
    g_log.rotation_bytes = max_bytes;
    g_log.rotation_backups = backups < 1 ? 1 : backups;
    ql_mutex_unlock(g_log.mutex);
}

/* ---------------------------------------------------------------------------
 * Writing
 * ------------------------------------------------------------------------ */

/** @brief Write one rendered line to every enabled sink. */
static void ql_log_emit(const char* text, size_t length)
{
    if ((g_log.sinks & QL_LOG_SINK_CONSOLE) != 0U)
    {
        (void)fwrite(text, 1, length, stderr);
        (void)fputc('\n', stderr);
    }

    if ((g_log.sinks & QL_LOG_SINK_FILE) != 0U && g_log.file != NULL && g_log.path != NULL)
    {
        if (g_log.rotation_bytes > 0 && g_log.written + length + 1 > g_log.rotation_bytes)
        {
            ql_log_rotate();
        }

        if (g_log.file != NULL)
        {
            (void)fwrite(text, 1, length, g_log.file);
            (void)fputc('\n', g_log.file);
            g_log.written += length + 1;
        }
    }
}

/** @brief Render one record into @p out. */
static void ql_log_render(char* out, size_t cap, ql_log_level_t level, const char* file, int line, const char* func,
                          const char* message)
{
    const char* name = ql_log_basename(file != NULL ? file : "?");

    if (g_log.format == QL_LOG_FORMAT_RAW)
    {
        (void)snprintf(out, cap, "%s", message);
        return;
    }

    if (g_log.format == QL_LOG_FORMAT_JSON)
    {
        char escaped[QL_LOG_LINE_MAX];
        char where[QL_LOG_LINE_MAX];

        ql_log_json_escape(escaped, sizeof escaped, message);
        ql_log_json_escape(where, sizeof where, name);
        (void)snprintf(out, cap, "{\"level\":\"%s\",\"file\":\"%s\",\"line\":%d,\"func\":\"%s\",\"msg\":\"%s\"}",
                       g_level_names[level], where, line, func != NULL ? func : "?", escaped);
        return;
    }

    {
        char stamp[32];
        size_t used;

        ql_log_timestamp(stamp, sizeof stamp);
        used = (size_t)snprintf(out, cap, "%s ", stamp);

        if (used < cap && g_log.color)
        {
            static const char* const colours[] = {
                "\033[90m", "\033[36m", "\033[32m", "\033[33m", "\033[31m", "\033[1;31m", "",
            };
            const char* colour = level >= QL_LOG_TRACE && level <= QL_LOG_OFF ? colours[level] : "";

            used += (size_t)snprintf(out + used, cap - used, "%s%-5s\033[0m ", colour, g_level_labels[level]);
        }
        else if (used < cap)
        {
            used += (size_t)snprintf(out + used, cap - used, "%-5s ", g_level_labels[level]);
        }

        if (used < cap)
        {
            (void)snprintf(out + used, cap - used, "%s:%d %s", name, line, message);
        }
    }
}

void ql_log_vwrite(ql_log_level_t level, const char* file, int line, const char* func, const char* fmt, va_list ap)
{
    char message[QL_LOG_LINE_MAX];
    char rendered[QL_LOG_LINE_MAX];
    int written;

    if (fmt == NULL || level < QL_LOG_TRACE || level > QL_LOG_OFF || !ql_log_ensure())
    {
        return;
    }
    if (!ql_log_enabled(level))
    {
        return;
    }

    written = vsnprintf(message, sizeof message, fmt, ap);
    if (written < 0)
    {
        message[0] = '\0';
    }

    ql_mutex_lock(g_log.mutex);

    ql_log_render(rendered, sizeof rendered, level, file, line, func, message);
    ql_log_emit(rendered, strlen(rendered));

    /* A fatal record is the last one before something goes wrong, so it must
     * not sit in a buffer. Ordinary records rely on ql_log_flush(). */
    if (level == QL_LOG_FATAL && g_log.file != NULL)
    {
        (void)fflush(g_log.file);
    }

    ql_mutex_unlock(g_log.mutex);
}

void ql_log_write(ql_log_level_t level, const char* file, int line, const char* func, const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    ql_log_vwrite(level, file, line, func, fmt, ap);
    va_end(ap);
}
