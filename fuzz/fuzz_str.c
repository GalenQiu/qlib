#include "qlib/ql_str.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * libFuzzer entry point for ql_str.
 *
 * The properties worth checking are the ones the header promises:
 *
 *   - the length is authoritative and equals what was appended, embedded NUL
 *     bytes included;
 *   - the buffer is always NUL terminated at the length, so ql_str_cstr() is
 *     safe to hand to any C function;
 *   - appending a string to itself duplicates it rather than corrupting it,
 *     which is the path the aliasing guard exists for;
 *   - assigning a prefix of itself works.
 */

/* Keeps the two doublings below inside a sane size. */
#define FUZZ_MAX_INPUT 4096

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    ql_str_t* str = NULL;
    size_t n = size < FUZZ_MAX_INPUT ? size : FUZZ_MAX_INPUT;

    if (ql_str_create(NULL, &str) != QL_OK)
    {
        abort();
    }

    /* An empty string must still be a usable C string. */
    if (ql_str_cstr(str) == NULL || ql_str_cstr(str)[0] != '\0' || ql_str_size(str) != 0)
    {
        abort();
    }

    if (ql_str_assign_n(str, (const char*)data, n) != QL_OK)
    {
        abort();
    }
    if (ql_str_size(str) != n)
    {
        abort();
    }
    /* The terminator sits at the length, whatever the content holds. */
    if (ql_str_cstr(str)[n] != '\0')
    {
        abort();
    }
    /* The bytes were copied verbatim, NULs included. */
    if (n != 0 && memcmp(ql_str_cstr(str), data, n) != 0)
    {
        abort();
    }
    /* The C string view stops at the first NUL, so it is never longer. */
    if (strlen(ql_str_cstr(str)) > n)
    {
        abort();
    }

    /* Appending the same bytes again doubles the length and duplicates them. */
    if (ql_str_append_n(str, (const char*)data, n) != QL_OK)
    {
        abort();
    }
    if (ql_str_size(str) != 2 * n)
    {
        abort();
    }
    if (n != 0 && (memcmp(ql_str_cstr(str), data, n) != 0 || memcmp(ql_str_cstr(str) + n, data, n) != 0))
    {
        abort();
    }

    /* Self append: the aliasing path, which may reallocate underneath. */
    if (ql_str_append_str(str, str) != QL_OK)
    {
        abort();
    }
    if (ql_str_size(str) != 4 * n)
    {
        abort();
    }
    if (n != 0 && memcmp(ql_str_cstr(str), ql_str_cstr(str) + (2 * n), 2 * n) != 0)
    {
        abort();
    }

    /* Assigning a slice of itself keeps the length honest. */
    if (n != 0)
    {
        if (ql_str_assign_n(str, ql_str_cstr(str) + n, n) != QL_OK)
        {
            abort();
        }
        if (ql_str_size(str) != n)
        {
            abort();
        }
        if (memcmp(ql_str_cstr(str), data, n) != 0)
        {
            abort();
        }
    }

    /* Comparison with itself is always equal, and find at offset 0 always hits. */
    if (ql_str_compare(str, str) != 0 || !ql_str_equal(str, str))
    {
        abort();
    }

    {
        size_t index = 12345;

        if (ql_str_find(str, "", &index) != QL_OK || index != 0)
        {
            abort();
        }
    }

    ql_str_destroy(str);
    return 0;
}
