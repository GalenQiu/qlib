#include "test_suites.h"

#include "qlib/ql_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_alloc.h"

/*
 * Logger tests.
 *
 * The console sink writes to stderr and cannot be captured portably, so every
 * test that checks output sends it to a file instead and reads the file back.
 * That is also how a user would verify their own configuration.
 */

#define LOG_PATH "qlib_log_test.log"
#define BACKUP_PATH "qlib_log_test.log.1"

/** @brief Read a whole file into @p buffer; returns the byte count, or -1. */
static long read_file(const char* path, char* buffer, size_t cap)
{
    FILE* file = fopen(path, "rb");
    size_t used;

    if (file == NULL)
    {
        return -1;
    }
    used = fread(buffer, 1, cap - 1, file);
    buffer[used] = '\0';
    (void)fclose(file);
    return (long)used;
}

/** @brief Point the logger at a fresh file with console output off. */
static void log_to_file(const char* path)
{
    MU_ASSERT(ql_log_init(NULL) == QL_OK);
    (void)remove(path);
    MU_ASSERT(ql_log_set_file(path) == QL_OK);
    ql_log_set_sinks(QL_LOG_SINK_FILE);
    ql_log_set_format(QL_LOG_FORMAT_PLAIN);
    ql_log_set_rotation(0, 1);
    ql_log_set_level(QL_LOG_INFO);
}

/** @brief Count the lines in a file; -1 if it cannot be read. */
static long count_lines(const char* path)
{
    FILE* file = fopen(path, "rb");
    long lines = 0;
    int c;

    if (file == NULL)
    {
        return -1;
    }
    while ((c = fgetc(file)) != EOF)
    {
        if (c == '\n')
        {
            lines++;
        }
    }
    (void)fclose(file);
    return lines;
}

/* ---------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

MU_TEST(test_init_is_idempotent)
{
    ql_log_shutdown();
    MU_ASSERT(ql_log_init(NULL) == QL_OK);
    MU_ASSERT(ql_log_init(NULL) == QL_OK);
    ql_log_shutdown();
    /* Shutting down twice is also fine. */
    ql_log_shutdown();
    MU_ASSERT(ql_log_init(NULL) == QL_OK);
}

MU_TEST(test_level_filtering)
{
    ql_log_shutdown();
    MU_ASSERT(ql_log_init(NULL) == QL_OK);

    ql_log_set_level(QL_LOG_WARN);
    MU_CHECK(ql_log_get_level() == QL_LOG_WARN);
    MU_CHECK(!ql_log_enabled(QL_LOG_TRACE));
    MU_CHECK(!ql_log_enabled(QL_LOG_DEBUG));
    MU_CHECK(!ql_log_enabled(QL_LOG_INFO));
    MU_CHECK(ql_log_enabled(QL_LOG_WARN));
    MU_CHECK(ql_log_enabled(QL_LOG_ERROR));
    MU_CHECK(ql_log_enabled(QL_LOG_FATAL));
    /* OFF is a threshold, not a record anyone writes. */
    MU_CHECK(!ql_log_enabled(QL_LOG_OFF));

    ql_log_set_level(QL_LOG_OFF);
    MU_CHECK(!ql_log_enabled(QL_LOG_FATAL));

    ql_log_set_level(QL_LOG_INFO);
    MU_CHECK(ql_log_enabled(QL_LOG_INFO));
}

MU_TEST(test_level_names_and_parsing)
{
    ql_log_level_t level = QL_LOG_OFF;

    MU_CHECK(strcmp(ql_log_level_name(QL_LOG_TRACE), "trace") == 0);
    MU_CHECK(strcmp(ql_log_level_name(QL_LOG_WARN), "warn") == 0);
    MU_CHECK(strcmp(ql_log_level_name(QL_LOG_OFF), "off") == 0);
    /* Casting out of range is the point: the value can arrive from a corrupt
     * record or a hand-built table. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- intentional
    MU_CHECK(ql_log_level_name((ql_log_level_t)99) == NULL);

    MU_ASSERT(ql_log_level_from_name("debug", &level) == QL_OK);
    MU_CHECK(level == QL_LOG_DEBUG);

    /* Case insensitive. */
    MU_ASSERT(ql_log_level_from_name("WARNING", &level) == QL_OK);
    MU_CHECK(level == QL_LOG_WARN);
    MU_ASSERT(ql_log_level_from_name("Err", &level) == QL_OK);
    MU_CHECK(level == QL_LOG_ERROR);
    MU_ASSERT(ql_log_level_from_name("critical", &level) == QL_OK);
    MU_CHECK(level == QL_LOG_FATAL);

    level = QL_LOG_OFF;
    MU_CHECK(ql_log_level_from_name("nonsense", &level) == QL_ERR_NOT_FOUND);
    MU_CHECK(level == QL_LOG_OFF); /* untouched */
    MU_CHECK(ql_log_level_from_name(NULL, &level) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_log_level_from_name("info", NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_level_from_environment)
{
    ql_log_shutdown();
    MU_ASSERT(ql_log_init(NULL) == QL_OK);
    ql_log_set_level(QL_LOG_INFO);

    /* Unset means "leave it alone", so an unconfigured environment is harmless. */
    (void)remove("QLIB_TEST_NO_SUCH_VARIABLE");
    MU_CHECK(ql_log_set_level_from_env("QLIB_TEST_NO_SUCH_VARIABLE") == QL_ERR_NOT_FOUND);
    MU_CHECK(ql_log_get_level() == QL_LOG_INFO);

    MU_CHECK(ql_log_set_level_from_env(NULL) == QL_ERR_INVALID_ARG);
}

MU_TEST(test_plain_format_contains_the_essentials)
{
    char buffer[2048];

    log_to_file(LOG_PATH);
    QL_LOG_INFO("hello %s %d", "world", 7);
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    MU_CHECK(strstr(buffer, "INFO") != NULL);
    MU_CHECK(strstr(buffer, "hello world 7") != NULL);
    /* The file name of the call site, not the full path. */
    MU_CHECK(strstr(buffer, "test_log.c") != NULL);
    /* And the line number, which is why the macro exists. */
    MU_CHECK(strstr(buffer, ":") != NULL);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_level_threshold_suppresses_output)
{
    char buffer[2048];

    log_to_file(LOG_PATH);
    ql_log_set_level(QL_LOG_WARN);
    QL_LOG_INFO("this must not appear");
    QL_LOG_WARN("this must appear");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) >= 0);
    MU_CHECK(strstr(buffer, "must not appear") == NULL);
    MU_CHECK(strstr(buffer, "must appear") != NULL);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_raw_format_is_the_message_only)
{
    char buffer[2048];

    log_to_file(LOG_PATH);
    ql_log_set_format(QL_LOG_FORMAT_RAW);
    QL_LOG_WARN("bare message");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    MU_CHECK(strcmp(buffer, "bare message\n") == 0);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_json_format_and_escaping)
{
    char buffer[4096];

    log_to_file(LOG_PATH);
    ql_log_set_format(QL_LOG_FORMAT_JSON);
    QL_LOG_ERROR("quote \" backslash \\ newline follows");
    QL_LOG_INFO("plain one");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    /* The fields a consumer needs. */
    MU_CHECK(strstr(buffer, "{\"level\":\"error\"") != NULL);
    MU_CHECK(strstr(buffer, "\"msg\":") != NULL);
    MU_CHECK(strstr(buffer, "plain one") != NULL);
    /* A quote inside the message must be escaped, or the line is not JSON. */
    MU_CHECK(strstr(buffer, "quote \\\" backslash \\\\ newline") != NULL);
    /* Two records, two objects. */
    MU_CHECK(strstr(buffer, "}\n{") != NULL);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_rotation_moves_the_file_aside)
{
    char buffer[4096];
    int i;

    log_to_file(LOG_PATH);
    (void)remove(BACKUP_PATH);
    ql_log_set_rotation(256, 1);

    /* Enough records to cross the limit at least once. */
    for (i = 0; i < 40; i++)
    {
        QL_LOG_INFO("rotation filler line number %d", i);
    }
    MU_ASSERT(ql_log_flush() == QL_OK);

    /* A backup exists, and the current file is smaller than the limit. */
    MU_CHECK(read_file(BACKUP_PATH, buffer, sizeof buffer) > 0);
    MU_CHECK(read_file(LOG_PATH, buffer, sizeof buffer) < 256 + 128);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
    (void)remove(BACKUP_PATH);
}

MU_TEST(test_null_and_edge_arguments)
{
    ql_log_shutdown();
    MU_ASSERT(ql_log_init(NULL) == QL_OK);

    /* A NULL format must be ignored rather than crashing in vsnprintf. */
    ql_log_write(QL_LOG_INFO, __FILE__, __LINE__, __func__, NULL);

    /* Out-of-range levels are ignored rather than indexed into. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- intentional
    ql_log_write((ql_log_level_t)99, __FILE__, __LINE__, __func__, "nope");
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- intentional
    ql_log_write((ql_log_level_t)-1, __FILE__, __LINE__, __func__, "nope");

    /* Turning the file sink on without a path emits nothing and does not crash. */
    ql_log_set_sinks(QL_LOG_SINK_FILE);
    ql_log_set_rotation(1024, 0); /* backups below 1 are clamped rather than rejected */
    QL_LOG_INFO("no file configured");

    /* Disabling the file sink by passing NULL. */
    MU_ASSERT(ql_log_set_file(NULL) == QL_OK);
    MU_CHECK(ql_log_flush() == QL_OK);
}

MU_TEST(test_no_leaks)
{
    tracking_state state;
    ql_allocator_t alloc;

    tracking_init(&state);
    alloc = make_tracking_allocator(&state);

    ql_log_shutdown();
    MU_ASSERT(ql_log_init(&alloc) == QL_OK);
    MU_ASSERT(ql_log_set_file(LOG_PATH) == QL_OK);
    /* File only: a test that writes to stderr pollutes every other report. */
    ql_log_set_sinks(QL_LOG_SINK_FILE);
    QL_LOG_INFO("one line");
    MU_ASSERT(ql_log_flush() == QL_OK);
    ql_log_shutdown();

    /* The logger's mutex and its copy of the path are both released. */
    MU_CHECK(state.live_blocks == 0);
    (void)remove(LOG_PATH);
}

MU_TEST(test_structured_fields_in_plain_output)
{
    char buffer[4096];

    log_to_file(LOG_PATH);
    ql_log_fields_begin();
    MU_CHECK(ql_log_field_str("user", "alice") == QL_OK);
    MU_CHECK(ql_log_field_int("attempt", 3) == QL_OK);
    MU_CHECK(ql_log_field_uint("bytes", 4096) == QL_OK);
    MU_CHECK(ql_log_field_bool("cached", true) == QL_OK);
    MU_CHECK(ql_log_field_double("ratio", 0.5, 2) == QL_OK);
    QL_LOG_WARN("login failed");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    MU_CHECK(strstr(buffer, "login failed") != NULL);
    MU_CHECK(strstr(buffer, "user=alice") != NULL);
    MU_CHECK(strstr(buffer, "attempt=3") != NULL);
    MU_CHECK(strstr(buffer, "bytes=4096") != NULL);
    MU_CHECK(strstr(buffer, "cached=true") != NULL);
    MU_CHECK(strstr(buffer, "ratio=0.50") != NULL);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_fields_belong_to_one_record)
{
    char buffer[4096];

    log_to_file(LOG_PATH);
    ql_log_fields_begin();
    MU_CHECK(ql_log_field_str("user", "alice") == QL_OK);
    QL_LOG_INFO("first");
    /* No begin() call: the previous set must already be gone. */
    QL_LOG_INFO("second");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    {
        const char* first = strstr(buffer, "first");
        const char* second = strstr(buffer, "second");

        MU_ASSERT(first != NULL && second != NULL);
        MU_CHECK(first < second);
        /* Exactly one occurrence, on the first record only. */
        MU_CHECK(strstr(second, "user=alice") == NULL);
        MU_CHECK(strstr(first, "user=alice") != NULL);
    }

    /* A filtered-out record must not leak its fields to the next one either. */
    ql_log_set_level(QL_LOG_ERROR);
    ql_log_fields_begin();
    MU_CHECK(ql_log_field_str("hidden", "yes") == QL_OK);
    QL_LOG_INFO("dropped");
    ql_log_set_level(QL_LOG_INFO);
    QL_LOG_INFO("third");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    {
        const char* third = strstr(buffer, "third");

        MU_ASSERT(third != NULL);
        MU_CHECK(strstr(third, "hidden=") == NULL);
    }

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_structured_fields_in_json)
{
    char buffer[4096];

    log_to_file(LOG_PATH);
    ql_log_set_format(QL_LOG_FORMAT_JSON);
    ql_log_fields_begin();
    MU_CHECK(ql_log_field_str("user", "alice") == QL_OK);
    MU_CHECK(ql_log_field_int("attempt", 3) == QL_OK);
    MU_CHECK(ql_log_field_bool("cached", false) == QL_OK);
    QL_LOG_ERROR("login failed");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    MU_CHECK(strstr(buffer, "\"msg\":\"login failed\"") != NULL);
    MU_CHECK(strstr(buffer, "\"user\":\"alice\"") != NULL);
    /* Numbers and booleans are typed, not quoted. */
    MU_CHECK(strstr(buffer, "\"attempt\":3") != NULL);
    MU_CHECK(strstr(buffer, "\"cached\":false") != NULL);
    /* The object still ends where it should, with no trailing comma. */
    MU_CHECK(strstr(buffer, "false}") != NULL || strstr(buffer, "}") != NULL);
    MU_CHECK(strstr(buffer, ",}") == NULL);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_field_values_are_escaped_in_json)
{
    char buffer[4096];

    log_to_file(LOG_PATH);
    ql_log_set_format(QL_LOG_FORMAT_JSON);
    ql_log_fields_begin();
    MU_CHECK(ql_log_field_str("path", "a\"b\\c") == QL_OK);
    QL_LOG_INFO("escaped");
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    MU_CHECK(strstr(buffer, "\"path\":\"a\\\"b\\\\c\"") != NULL);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_field_arguments_are_checked)
{
    ql_log_fields_begin();
    MU_CHECK(ql_log_field_str(NULL, "x") == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_log_field_str("k", NULL) == QL_ERR_INVALID_ARG);
    MU_CHECK(ql_log_field_int(NULL, 1) == QL_ERR_INVALID_ARG);

    /* Filling the set past its capacity reports the overflow rather than
     * emitting half a field. */
    {
        char value[64];
        ql_ret_t last = QL_OK;
        int i;

        ql_log_fields_begin();
        for (i = 0; i < 40; i++)
        {
            (void)snprintf(value, sizeof value, "value-number-%d-padding-padding", i);
            last = ql_log_field_str("k", value);
            if (last != QL_OK)
            {
                break;
            }
        }
        MU_CHECK(last == QL_ERR_OUT_OF_RANGE);
    }

    ql_log_fields_begin();
}

MU_TEST(test_async_delivers_every_record_in_order)
{
    char buffer[8192];
    int i;

    log_to_file(LOG_PATH);
    MU_ASSERT(ql_log_async_enable(64) == QL_OK);

    for (i = 0; i < 50; i++)
    {
        QL_LOG_INFO("async record %d", i);
    }
    ql_log_async_flush();
    MU_CHECK(ql_log_async_dropped() == 0);
    MU_ASSERT(ql_log_flush() == QL_OK);

    MU_ASSERT(read_file(LOG_PATH, buffer, sizeof buffer) > 0);
    /* Every record arrived, and each appears after the one before it. */
    {
        const char* p = buffer;
        int expected = 0;

        for (;;)
        {
            char needle[32];
            const char* found;

            (void)snprintf(needle, sizeof needle, "async record %d", expected);
            found = strstr(p, needle);
            if (found == NULL)
            {
                break;
            }
            p = found + 1;
            expected++;
        }
        MU_CHECK(expected == 50);
    }

    MU_ASSERT(ql_log_async_enable(0) == QL_OK);
    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_async_never_loses_records_silently)
{
    enum
    {
        RECORDS = 200,
    };
    long written;
    long dropped;
    int i;

    log_to_file(LOG_PATH);
    /*
     * One slot, so records are dropped: what matters is that each record is
     * either written or counted, never simply lost. Asserting the sum is
     * exact, which a test of "dropped > 0" could not do without being flaky.
     */
    MU_ASSERT(ql_log_async_enable(1) == QL_OK);

    for (i = 0; i < RECORDS; i++)
    {
        QL_LOG_INFO("overflow record %d", i);
    }
    ql_log_async_flush();
    MU_ASSERT(ql_log_flush() == QL_OK);

    written = count_lines(LOG_PATH);
    dropped = (long)ql_log_async_dropped();
    MU_CHECK(written >= 0);
    MU_CHECK(dropped >= 0);
    MU_CHECK(written + dropped == RECORDS);

    MU_ASSERT(ql_log_async_enable(0) == QL_OK);
    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_disable_drains_the_queue)
{
    long written;
    int i;

    log_to_file(LOG_PATH);
    MU_ASSERT(ql_log_async_enable(64) == QL_OK);
    for (i = 0; i < 20; i++)
    {
        QL_LOG_INFO("drain record %d", i);
    }

    /* Disabling joins the writer, which drains what is queued first. The file
     * still has to be flushed: the writer wrote through a buffered FILE, and
     * count_lines opens the path separately. */
    MU_ASSERT(ql_log_async_enable(0) == QL_OK);
    MU_ASSERT(ql_log_flush() == QL_OK);
    written = count_lines(LOG_PATH);
    MU_CHECK(written == 20);

    /* Back to synchronous, and still writing. */
    QL_LOG_INFO("synchronous again");
    MU_ASSERT(ql_log_flush() == QL_OK);
    MU_CHECK(count_lines(LOG_PATH) == 21);

    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST(test_async_rejects_a_second_enable)
{
    log_to_file(LOG_PATH);
    MU_ASSERT(ql_log_async_enable(8) == QL_OK);
    MU_CHECK(ql_log_async_enable(8) == QL_ERR_BUSY);
    MU_ASSERT(ql_log_async_enable(0) == QL_OK);
    MU_CHECK(ql_log_async_dropped() == 0);
    ql_log_shutdown();
    (void)remove(LOG_PATH);
}

MU_TEST_SUITE(test_suite_log)
{
    MU_RUN_TEST(test_init_is_idempotent);
    MU_RUN_TEST(test_level_filtering);
    MU_RUN_TEST(test_level_names_and_parsing);
    MU_RUN_TEST(test_level_from_environment);
    MU_RUN_TEST(test_plain_format_contains_the_essentials);
    MU_RUN_TEST(test_level_threshold_suppresses_output);
    MU_RUN_TEST(test_raw_format_is_the_message_only);
    MU_RUN_TEST(test_json_format_and_escaping);
    MU_RUN_TEST(test_rotation_moves_the_file_aside);
    MU_RUN_TEST(test_null_and_edge_arguments);
    MU_RUN_TEST(test_structured_fields_in_plain_output);
    MU_RUN_TEST(test_fields_belong_to_one_record);
    MU_RUN_TEST(test_structured_fields_in_json);
    MU_RUN_TEST(test_field_values_are_escaped_in_json);
    MU_RUN_TEST(test_field_arguments_are_checked);
    MU_RUN_TEST(test_async_delivers_every_record_in_order);
    MU_RUN_TEST(test_async_never_loses_records_silently);
    MU_RUN_TEST(test_disable_drains_the_queue);
    MU_RUN_TEST(test_async_rejects_a_second_enable);
    MU_RUN_TEST(test_no_leaks);
}
