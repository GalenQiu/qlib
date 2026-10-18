/*
 * qlib workbench -- a terminal dashboard built on the whole library.
 *
 * This is not a "hello world". It is what using qlib looks like when several
 * modules are in play at once:
 *
 *   ql_thread   worker threads, a mutex, and a condition variable used as a
 *               pacer (a 1 ms timed wait rather than a spin)
 *   ql_hash     per-worker counters
 *   ql_list     a bounded queue of recent records, oldest dropped
 *   ql_vector   a rolling window of throughput samples for the sparkline
 *   ql_str      every rendered line is assembled in one
 *   ql_log      the same records are also written to a log file, asynchronously
 *   ql_net      an address is parsed and formatted to show the net panel
 *   ql_mem      the hash, the list, the vector, the strings and the sockets all
 *               take their memory from one allocator
 *
 * Rendering is plain ANSI escapes and ASCII box drawing, so it works over SSH
 * and in any terminal that understands colour codes. It does not need a TTY: it
 * just writes, which is also what makes it testable with --frames.
 *
 * Usage:
 *   qlib_workbench                 run until Ctrl-C
 *   qlib_workbench --frames 20     render 20 frames and exit (for a smoke test)
 *   qlib_workbench --workers 8     how many worker threads
 *   qlib_workbench --fps 20        frames per second
 *   qlib_workbench --log FILE      write the record stream to FILE
 */

#include <qlib/qlib.h>

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MESSAGES 12
#define SPARK_WIDTH 60
#define RENDER_WIDTH 78
#define MAX_WORKERS 16
#define RECORD_EVERY 25

/* Roughly " .:-=+*#%@", indexed by a 0..9 level. */
static const char g_spark_chars[] = " .:-=+*#%@";

typedef struct
{
    ql_mutex_t* mutex;
    ql_cond_t* pacer;
    ql_list_t* messages;
    ql_hash_t* per_worker;
    ql_vector_t* samples;
    size_t total;
    size_t dropped;
    bool running;
} workbench;

typedef struct
{
    workbench* wb;
    int id;
} worker_arg;

typedef struct
{
    int workers;
    int frames;
    int fps;
    const char* log_path;
} workbench_options;

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

/** @brief Sleep by waiting on a condition variable that nobody signals. */
static void sleep_ms(workbench* wb, int millis)
{
    ql_mutex_lock(wb->mutex);
    (void)ql_cond_wait(wb->pacer, wb->mutex, millis);
    ql_mutex_unlock(wb->mutex);
}

/** @brief Append a record to the bounded queue, dropping the oldest. */
static void push_message(workbench* wb, const char* text)
{
    ql_list_node_t* node = NULL;

    if (ql_list_push_back(wb->messages, text, &node) != QL_OK)
    {
        wb->dropped++;
        return;
    }
    while (ql_list_size(wb->messages) > MAX_MESSAGES)
    {
        (void)ql_list_pop_front(wb->messages, NULL);
    }
}

/*
 * The worker: do a chunk of arithmetic, publish the result, sleep a millisecond.
 *
 * The arithmetic is meaningless on purpose -- what matters is that the shared
 * state is only ever touched under the mutex, and that the record stream shows
 * real work happening.
 */
static void worker(void* arg)
{
    worker_arg* entry = arg;
    workbench* wb = entry->wb;
    /* Seed per worker, or every thread computes the same sequence and the
     * record stream looks broken rather than busy. */
    unsigned long long local = ((unsigned long long)entry->id * 2654435761ULL) + 1ULL;
    int iterations = 0;

    for (;;)
    {
        size_t done = 0;
        size_t previous = 0;
        char text[64];
        int i;

        for (i = 0; i < 20000; i++)
        {
            local = (local * 1103515245ULL) + 12345ULL;
            local ^= local >> 17U;
        }

        /* Build the message first: it touches only this thread's state. */
        text[0] = '\0';
        iterations++;
        if (iterations % RECORD_EVERY == 0)
        {
            ql_str_t* line = NULL;

            if (ql_str_create(NULL, &line) == QL_OK)
            {
                if (ql_str_append_printf(line, "worker %d published %llu", entry->id, local) == QL_OK)
                {
                    (void)snprintf(text, sizeof text, "%s", ql_str_cstr(line));
                }
                ql_str_destroy(line);
            }
        }

        /* Then publish everything under the one lock. ql_hash and ql_list are
         * not thread-safe, which is exactly what ThreadSanitizer said when the
         * first version of this demo updated the counters outside it. */
        ql_mutex_lock(wb->mutex);
        if (!wb->running)
        {
            ql_mutex_unlock(wb->mutex);
            return;
        }
        wb->total += 20000;
        if (ql_hash_get(wb->per_worker, &entry->id, &previous) == QL_OK)
        {
            previous++;
        }
        else
        {
            previous = 1;
        }
        (void)ql_hash_set(wb->per_worker, &entry->id, &previous);
        if (text[0] != '\0')
        {
            push_message(wb, text);
        }
        done = wb->total;
        ql_mutex_unlock(wb->mutex);

        if (text[0] != '\0')
        {
            QL_LOG_DEBUG("worker %d total=%zu", entry->id, done);
        }

        sleep_ms(wb, 1);
    }
}

/*
 * Rendering is best effort. A line that does not fit is truncated, and there is
 * nothing useful to do about it mid-frame -- dropping the rest of the dashboard
 * would be worse than a short line. These three wrappers are where that decision
 * lives: the ignore is stated once, here, instead of being scattered over every
 * call in the renderer.
 */
static void line_append(ql_str_t* line, const char* text)
{
    ql_ret_t rc = ql_str_append(line, text);

    (void)rc;
}

static void line_append_char(ql_str_t* line, char c)
{
    ql_ret_t rc = ql_str_append_char(line, c);

    (void)rc;
}

static void line_printf(ql_str_t* line, const char* format, ...)
{
    va_list ap;
    ql_ret_t rc;

    va_start(ap, format);
    rc = ql_str_append_vprintf(line, format, ap);
    va_end(ap);
    (void)rc;
}

/** @brief Draw a horizontal rule that fits the render width. */
static void rule(ql_str_t* line, const char* left, const char* fill, const char* right)
{
    int i;

    line_append(line, left);
    for (i = 0; i < RENDER_WIDTH - 2; i++)
    {
        line_append(line, fill);
    }
    line_append(line, right);
}

/** @brief Emit one assembled line. */
static void emit(const ql_str_t* line)
{
    (void)fputs(ql_str_cstr(line), stdout);
    (void)fputc('\n', stdout);
}

/** @brief Draw the header: uptime, workers, and what the queue is doing. */
static void render_header(ql_str_t* line, double seconds, int workers)
{
    line_append(line, "\033[1;36m qlib ");
    line_printf(line, "workbench " QLIB_VERSION_STRING "\033[0m");
    line_printf(line, "   up %.1fs   threads %d   width %d", seconds, workers, RENDER_WIDTH);
}

/** @brief Draw the sparkline of recent throughput. */
static void render_spark(ql_str_t* line, const workbench* wb)
{
    size_t i;
    size_t count = ql_vector_size(wb->samples);
    size_t peak = 1;
    size_t start = count > SPARK_WIDTH ? count - SPARK_WIDTH : 0;

    for (i = start; i < count; i++)
    {
        const size_t* sample = ql_vector_at(wb->samples, i);

        if (sample != NULL && *sample > peak)
        {
            peak = *sample;
        }
    }

    line_append(line, " ");
    for (i = start; i < count; i++)
    {
        const size_t* sample = ql_vector_at(wb->samples, i);
        size_t level = sample != NULL ? (*sample * 9) / peak : 0;

        if (level > 9)
        {
            level = 9;
        }
        line_append_char(line, g_spark_chars[level]);
    }
}

/** @brief Draw the per-worker counters, from the hash table. */
static void render_counters(ql_str_t* line, const workbench* wb, int workers)
{
    int id;

    for (id = 0; id < workers; id++)
    {
        size_t count = 0;

        if (ql_hash_get(wb->per_worker, &id, &count) != QL_OK)
        {
            count = 0;
        }
        line_printf(line, " w%-2d %6zu  ", id, count);
        if ((id % 3) == 2)
        {
            emit(line);
            ql_str_clear(line);
        }
    }
    if (!ql_str_empty(line))
    {
        emit(line);
        ql_str_clear(line);
    }
}

/** @brief Draw the recent records, newest last. */
static void render_messages(ql_str_t* line, const workbench* wb)
{
    ql_list_node_t* node;
    char text[64];

    for (node = ql_list_first(wb->messages); node != NULL; node = ql_list_next(node))
    {
        memset(text, 0, sizeof text);
        if (ql_list_node_value(wb->messages, node, text) != QL_OK || text[0] == '\0')
        {
            continue;
        }
        line_printf(line, " \033[90m|\033[0m %s", text);
        emit(line);
        ql_str_clear(line);
    }
}

/** @brief Draw one frame. */
static void render(workbench* wb, double seconds, int workers)
{
    ql_str_t* line = NULL;
    ql_net_addr_t net;
    char net_text[QL_NET_ADDR_STR_MAX];

    if (ql_str_create(NULL, &line) != QL_OK)
    {
        return;
    }

    /* Home the cursor and erase to the end: a full redraw without the flicker
     * of clearing the whole screen. */
    line_append(line, "\033[H");
    emit(line);
    ql_str_clear(line);

    line_append(line, "\033[1;36m");
    rule(line, "+", "-", "+");
    line_append(line, "\033[0m");
    emit(line);
    ql_str_clear(line);

    line_append(line, "|");
    render_header(line, seconds, workers);
    line_append(line, " |");
    emit(line);
    ql_str_clear(line);

    line_append(line, "+");
    rule(line, "+", "-", "+");
    emit(line);
    ql_str_clear(line);

    /* Throughput. */
    {
        size_t total;

        ql_mutex_lock(wb->mutex);
        total = wb->total;
        ql_mutex_unlock(wb->mutex);

        line_printf(line, "| total %zu   queue %zu/%d   dropped %zu", total, ql_list_size(wb->messages), MAX_MESSAGES,
                    wb->dropped);
        emit(line);
        ql_str_clear(line);

        line_append(line, "|");
        render_spark(line, wb);
        emit(line);
        ql_str_clear(line);
    }

    rule(line, "+", "-", "+");
    emit(line);
    ql_str_clear(line);

    /* Counters and records side by side would need a grid; the terminal width
     * is not knowable portably, so they are stacked. */
    line_append(line, "| per-worker counters");
    emit(line);
    ql_str_clear(line);
    ql_mutex_lock(wb->mutex);
    render_counters(line, wb, workers);
    ql_mutex_unlock(wb->mutex);

    rule(line, "+", "-", "+");
    emit(line);
    ql_str_clear(line);

    line_append(line, "| recent records");
    emit(line);
    ql_str_clear(line);
    ql_mutex_lock(wb->mutex);
    render_messages(line, wb);
    ql_mutex_unlock(wb->mutex);

    rule(line, "+", "-", "+");
    emit(line);
    ql_str_clear(line);

    /* The network panel: parsing and formatting an address needs no network,
     * which is the point of keeping address handling separate from sockets. */
    memset(&net, 0, sizeof net);
    memset(net_text, 0, sizeof net_text);
    (void)ql_net_addr_parse("2001:db8::1", &net);
    (void)ql_net_addr_format(&net, net_text, sizeof net_text);
    line_printf(line, "| net: %s parses and formats as %s", "2001:db8::1", net_text);
    emit(line);
    ql_str_clear(line);

    line_append(line, "\033[J\033[1;90m");
    rule(line, "+", "-", "+");
    line_append(line, "\033[0m");
    emit(line);
    (void)fflush(stdout);

    ql_str_destroy(line);
}

static void usage(const char* program)
{
    (void)printf("usage: %s [--frames N] [--workers N] [--fps N] [--log FILE]\n", program);
    (void)printf("\n");
    (void)printf("  --frames N   render N frames and exit; 0 (the default) runs until Ctrl-C\n");
    (void)printf("  --workers N  worker threads, 1..%d (default 4)\n", MAX_WORKERS);
    (void)printf("  --fps N      frames per second, 1..60 (default 10)\n");
    (void)printf("  --log FILE   also write the record stream to FILE asynchronously\n");
}

/** @brief Parse a non-negative integer and reject anything else. */
static bool parse_uint(const char* text, int* out)
{
    char* end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || value < 0 || value > INT_MAX)
    {
        return false;
    }
    *out = (int)value;
    return true;
}

/**
 * @brief Read the command line.
 *
 * Returns 0 to carry on, 1 when the caller should exit successfully (--help), and
 * -1 on a bad option. atoi() would have been shorter and would have turned
 * "--frames abc" into 0 without a word.
 */
static int parse_options(int argc, char** argv, workbench_options* opts)
{
    int i;

    for (i = 1; i < argc; i++)
    {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
        {
            usage(argv[0]);
            return 1;
        }
        if (strcmp(arg, "--log") == 0)
        {
            if (i + 1 >= argc)
            {
                (void)fprintf(stderr, "%s: --log needs a file name\n", argv[0]);
                return -1;
            }
            opts->log_path = argv[++i];
            continue;
        }
        if (i + 1 >= argc)
        {
            (void)fprintf(stderr, "%s: %s needs a value\n", argv[0], arg);
            return -1;
        }
        if (strcmp(arg, "--frames") == 0)
        {
            if (!parse_uint(argv[++i], &opts->frames))
            {
                (void)fprintf(stderr, "%s: --frames needs a non-negative number\n", argv[0]);
                return -1;
            }
        }
        else if (strcmp(arg, "--workers") == 0)
        {
            if (!parse_uint(argv[++i], &opts->workers))
            {
                (void)fprintf(stderr, "%s: --workers needs a non-negative number\n", argv[0]);
                return -1;
            }
        }
        else if (strcmp(arg, "--fps") == 0)
        {
            if (!parse_uint(argv[++i], &opts->fps))
            {
                (void)fprintf(stderr, "%s: --fps needs a non-negative number\n", argv[0]);
                return -1;
            }
        }
        else
        {
            (void)fprintf(stderr, "%s: unknown option %s\n", argv[0], arg);
            usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    workbench wb;
    worker_arg args[MAX_WORKERS];
    ql_thread_t* threads[MAX_WORKERS];
    ql_allocator_t* alloc = NULL;
    int workers;
    int frames;
    int fps;
    const char* log_path;
    int rendered = 0;
    int i;
    workbench_options opts;
    int parsed;

    memset(&opts, 0, sizeof opts);
    opts.workers = 4;
    opts.fps = 10;

    parsed = parse_options(argc, argv, &opts);
    if (parsed == 1)
    {
        return 0;
    }
    if (parsed != 0)
    {
        return 2;
    }

    workers = opts.workers;
    frames = opts.frames;
    fps = opts.fps;
    log_path = opts.log_path;

    if (workers < 1 || workers > MAX_WORKERS || fps < 1 || fps > 60 || frames < 0)
    {
        (void)fprintf(stderr, "%s: workers must be 1..%d, fps 1..60, frames >= 0\n", argv[0], MAX_WORKERS);
        return 2;
    }

    memset(&wb, 0, sizeof wb);
    memset(args, 0, sizeof args);

    if (ql_log_init(alloc) != QL_OK)
    {
        (void)fprintf(stderr, "%s: could not start the logger\n", argv[0]);
        return 1;
    }
    QL_LOG_INFO("workbench starting: %d workers at %d fps", workers, fps);
    /* The demo's point is to show the record stream, so DEBUG is on. */
    ql_log_set_level(QL_LOG_DEBUG);
    if (log_path != NULL)
    {
        if (ql_log_set_file(log_path) != QL_OK)
        {
            (void)fprintf(stderr, "%s: could not open %s\n", argv[0], log_path);
            return 1;
        }
        (void)ql_log_async_enable(128);
    }

    if (ql_mutex_create(alloc, &wb.mutex) != QL_OK || ql_cond_create(alloc, &wb.pacer) != QL_OK ||
        ql_list_create(alloc, sizeof(char[64]), &wb.messages) != QL_OK ||
        ql_hash_create(alloc, sizeof(int), sizeof(size_t), &wb.per_worker) != QL_OK ||
        ql_vector_create(alloc, sizeof(size_t), &wb.samples) != QL_OK)
    {
        (void)fprintf(stderr, "%s: could not create the workbench state\n", argv[0]);
        return 1;
    }
    wb.running = true;

    for (i = 0; i < workers; i++)
    {
        args[i].wb = &wb;
        args[i].id = i;
        if (ql_thread_create(alloc, worker, &args[i], &threads[i]) != QL_OK)
        {
            (void)fprintf(stderr, "%s: could not start worker %d\n", argv[0], i);
            return 1;
        }
    }

    (void)signal(SIGINT, on_sigint);

    /* Hide the cursor and clear once; every frame after that homes and erases
     * to the end, which redraws without the flicker of a full clear. */
    (void)fputs("\033[?25l\033[2J", stdout);

    {
        int frame_ms = 1000 / (fps > 0 ? fps : 1);
        double elapsed = 0.0;

        while (!g_stop && (frames == 0 || rendered < frames))
        {
            size_t sample;

            ql_mutex_lock(&*wb.mutex);
            sample = wb.total;
            ql_mutex_unlock(wb.mutex);

            if (ql_vector_push_back(wb.samples, &sample) != QL_OK)
            {
                ql_vector_clear(wb.samples);
            }

            render(&wb, elapsed, workers);
            rendered++;
            elapsed += (double)frame_ms / 1000.0;
            sleep_ms(&wb, frame_ms);
        }
    }

    /* Shut the workers down under the mutex, then join them. */
    ql_mutex_lock(wb.mutex);
    wb.running = false;
    ql_mutex_unlock(wb.mutex);
    for (i = 0; i < workers; i++)
    {
        ql_thread_join(threads[i]);
    }

    (void)fputs("\033[?25h\033[0m\n", stdout);
    QL_LOG_INFO("workbench stopping after %d frames", rendered);

    if (log_path != NULL)
    {
        ql_log_async_flush();
        (void)ql_log_async_enable(0);
    }
    ql_log_shutdown();

    ql_vector_destroy(wb.samples);
    ql_hash_destroy(wb.per_worker);
    ql_list_destroy(wb.messages);
    ql_cond_destroy(wb.pacer);
    ql_mutex_destroy(wb.mutex);

    (void)printf("rendered %d frame(s)\n", rendered);
    return 0;
}
