#include "qlib/ql_str.h"

#include "ql_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Smallest buffer worth allocating; keeps short strings from reallocating. */
#define QL_STR_MIN_CAPACITY 16

/*
 * Invariants:
 *   data == NULL  =>  size == 0 and capacity == 0
 *   data != NULL  =>  data[size] == '\0' and capacity >= size
 *
 * The second is what lets ql_str_cstr() always return a usable C string, and
 * every mutating function below restores it before returning -- including on
 * the paths that fail part way through, which is why the printf retry loop
 * rewrites the terminator after a truncated attempt.
 */
struct ql_str
{
    ql_allocator_t alloc;
    char* data;
    size_t size;
    size_t capacity;
};

/** @brief Grow the buffer so it can hold @p min_capacity bytes plus a terminator. */
static ql_ret_t ql_str_grow(ql_str_t* str, size_t min_capacity)
{
    size_t new_capacity;
    size_t bytes;
    char* data;

    if (min_capacity <= str->capacity)
    {
        return QL_OK;
    }

    new_capacity = str->capacity < QL_STR_MIN_CAPACITY ? QL_STR_MIN_CAPACITY : str->capacity;
    while (new_capacity < min_capacity)
    {
        size_t doubled;

        if (ql_size_mul_overflow(new_capacity, 2, &doubled))
        {
            ql_err_raise(QL_ERR_OVERFLOW, "ql_str_grow: capacity overflow");
            return QL_ERR_OVERFLOW;
        }
        new_capacity = doubled;
    }

    if (ql_size_add_overflow(new_capacity, 1, &bytes))
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_str_grow: buffer size overflow");
        return QL_ERR_OVERFLOW;
    }

    data = ql_realloc(&str->alloc, str->data, bytes);
    if (data == NULL)
    {
        /* ql_realloc has already raised QL_ERR_NOMEM; the old buffer is intact. */
        return QL_ERR_NOMEM;
    }

    str->data = data;
    str->capacity = new_capacity;
    str->data[str->size] = '\0';
    return QL_OK;
}

/** @brief Grow so that @p extra more bytes fit at the end. */
static ql_ret_t ql_str_ensure(ql_str_t* str, size_t extra)
{
    size_t needed;

    if (ql_size_add_overflow(str->size, extra, &needed))
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_str: length overflow");
        return QL_ERR_OVERFLOW;
    }
    return ql_str_grow(str, needed);
}

/**
 * @brief Whether @p data points into the string's own buffer.
 *
 * Appending a string to itself is a reasonable thing to write, and the growth
 * step may move the buffer, so the offset has to be re-derived afterwards.
 * Comparing the addresses as integers is implementation-defined rather than
 * strictly portable, but it is the only way to answer the question without
 * knowing where the object was allocated; the alternative -- copying every
 * source into scratch space -- would allocate on every append.
 */
static bool ql_str_owns(const ql_str_t* str, const char* data)
{
    uintptr_t base;
    uintptr_t here;

    if (str->data == NULL || data == NULL)
    {
        return false;
    }

    base = (uintptr_t)str->data;
    here = (uintptr_t)data;
    return here >= base && here < base + str->capacity;
}

static ql_ret_t ql_str_append_impl(ql_str_t* str, const char* data, size_t len)
{
    bool owned;
    size_t offset = 0;
    ql_ret_t rc;

    if (str == NULL || (data == NULL && len != 0))
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_append: str or data is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (len == 0)
    {
        return QL_OK;
    }

    owned = ql_str_owns(str, data);
    if (owned)
    {
        offset = (size_t)(data - str->data);
    }

    rc = ql_str_ensure(str, len);
    if (rc != QL_OK)
    {
        return rc;
    }

    if (owned)
    {
        /* The buffer may have moved; the contents and the offset did not. */
        data = str->data + offset;
    }

    memcpy(str->data + str->size, data, len);
    str->size += len;
    str->data[str->size] = '\0';
    return QL_OK;
}

ql_ret_t ql_str_create(const ql_allocator_t* alloc, ql_str_t** out)
{
    ql_str_t* str;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_create: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    str = ql_alloc(alloc, sizeof *str);
    if (str == NULL)
    {
        return QL_ERR_NOMEM;
    }

    str->alloc = alloc != NULL ? *alloc : ql_default_allocator();
    str->data = NULL;
    str->size = 0;
    str->capacity = 0;

    *out = str;
    return QL_OK;
}

ql_ret_t ql_str_create_from(const ql_allocator_t* alloc, const char* cstr, ql_str_t** out)
{
    ql_str_t* str;
    ql_ret_t rc;

    if (cstr == NULL)
    {
        if (out != NULL)
        {
            *out = NULL;
        }
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_create_from: cstr is NULL");
        return QL_ERR_INVALID_ARG;
    }

    rc = ql_str_create(alloc, &str);
    if (rc != QL_OK)
    {
        return rc;
    }

    rc = ql_str_append(str, cstr);
    if (rc != QL_OK)
    {
        ql_str_destroy(str);
        return rc;
    }

    *out = str;
    return QL_OK;
}

void ql_str_destroy(ql_str_t* str)
{
    ql_allocator_t alloc;

    if (str == NULL)
    {
        return;
    }

    /* Copy the allocator out before the control block itself goes away. */
    alloc = str->alloc;
    ql_dealloc(&alloc, str->data);
    ql_dealloc(&alloc, str);
}

ql_ret_t ql_str_assign(ql_str_t* str, const char* cstr)
{
    if (cstr == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_assign: cstr is NULL");
        return QL_ERR_INVALID_ARG;
    }
    return ql_str_assign_n(str, cstr, strlen(cstr));
}

ql_ret_t ql_str_assign_n(ql_str_t* str, const char* data, size_t len)
{
    ql_ret_t rc;

    if (str == NULL || (data == NULL && len != 0))
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_assign_n: str or data is NULL");
        return QL_ERR_INVALID_ARG;
    }

    if (len == 0)
    {
        ql_str_clear(str);
        return QL_OK;
    }

    if (ql_str_owns(str, data))
    {
        /*
         * The source already lives inside the buffer, so it fits by
         * definition: shifting it to the front needs no allocation, and using
         * memmove makes an overlapping source safe.
         */
        memmove(str->data, data, len);
    }
    else
    {
        /* On failure ql_str_grow leaves the previous contents intact. */
        rc = ql_str_grow(str, len);
        if (rc != QL_OK)
        {
            return rc;
        }
        memcpy(str->data, data, len);
    }

    str->size = len;
    str->data[len] = '\0';
    return QL_OK;
}

ql_ret_t ql_str_append(ql_str_t* str, const char* cstr)
{
    if (cstr == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_append: cstr is NULL");
        return QL_ERR_INVALID_ARG;
    }
    return ql_str_append_impl(str, cstr, strlen(cstr));
}

ql_ret_t ql_str_append_n(ql_str_t* str, const char* data, size_t len)
{
    return ql_str_append_impl(str, data, len);
}

ql_ret_t ql_str_append_char(ql_str_t* str, char c)
{
    return ql_str_append_impl(str, &c, 1);
}

ql_ret_t ql_str_append_str(ql_str_t* str, const ql_str_t* other)
{
    if (other == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_append_str: other is NULL");
        return QL_ERR_INVALID_ARG;
    }
    return ql_str_append_impl(str, other->data, other->size);
}

ql_ret_t ql_str_append_vprintf(ql_str_t* str, const char* fmt, va_list ap)
{
    size_t room = 64;

    if (str == NULL || fmt == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_append_vprintf: str or fmt is NULL");
        return QL_ERR_INVALID_ARG;
    }

    for (;;)
    {
        va_list copy;
        size_t available;
        int written;
        ql_ret_t rc;

        rc = ql_str_ensure(str, room);
        if (rc != QL_OK)
        {
            return rc;
        }

        /* Reserve one byte past the room, for the terminator. */
        available = str->capacity - str->size;

        va_copy(copy, ap);
        written = vsnprintf(str->data + str->size, available + 1, fmt, copy);
        va_end(copy);

        if (written >= 0 && (size_t)written <= available)
        {
            str->size += (size_t)written;
            return QL_OK;
        }

        /* A truncated attempt may have written a partial result and no NUL. */
        str->data[str->size] = '\0';

        if (written >= 0)
        {
            /* The runtime reported how much it needed; ask for exactly that. */
            room = (size_t)written;
        }
        else if (ql_size_mul_overflow(room, 2, &room))
        {
            ql_err_raise(QL_ERR_OVERFLOW, "ql_str_append_vprintf: length overflow");
            return QL_ERR_OVERFLOW;
        }
    }
}

ql_ret_t ql_str_append_printf(ql_str_t* str, const char* fmt, ...)
{
    va_list ap;
    ql_ret_t rc;

    va_start(ap, fmt);
    rc = ql_str_append_vprintf(str, fmt, ap);
    va_end(ap);
    return rc;
}

ql_ret_t ql_str_reserve(ql_str_t* str, size_t capacity)
{
    if (str == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_reserve: str is NULL");
        return QL_ERR_INVALID_ARG;
    }
    return ql_str_grow(str, capacity);
}

void ql_str_clear(ql_str_t* str)
{
    if (str == NULL)
    {
        return;
    }

    str->size = 0;
    if (str->data != NULL)
    {
        str->data[0] = '\0';
    }
}

const char* ql_str_cstr(const ql_str_t* str)
{
    if (str == NULL)
    {
        return NULL;
    }
    /* Never return NULL for a valid string: an empty one yields "". */
    return str->data != NULL ? str->data : "";
}

size_t ql_str_size(const ql_str_t* str)
{
    return str != NULL ? str->size : 0;
}

size_t ql_str_capacity(const ql_str_t* str)
{
    return str != NULL ? str->capacity : 0;
}

bool ql_str_empty(const ql_str_t* str)
{
    return str == NULL || str->size == 0;
}

int ql_str_compare(const ql_str_t* a, const ql_str_t* b)
{
    size_t na = a != NULL ? a->size : 0;
    size_t nb = b != NULL ? b->size : 0;
    size_t shorter = na < nb ? na : nb;

    if (shorter > 0)
    {
        int rc = memcmp(a->data, b->data, shorter);
        if (rc != 0)
        {
            return rc;
        }
    }

    if (na < nb)
    {
        return -1;
    }
    if (na > nb)
    {
        return 1;
    }
    return 0;
}

bool ql_str_equal_cstr(const ql_str_t* str, const char* cstr)
{
    size_t size;

    if (str == NULL || cstr == NULL)
    {
        return false;
    }

    size = strlen(cstr);
    if (str->size != size)
    {
        return false;
    }
    return size == 0 || memcmp(str->data, cstr, size) == 0;
}

bool ql_str_equal(const ql_str_t* a, const ql_str_t* b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }
    if (a->size != b->size)
    {
        return false;
    }
    return a->size == 0 || memcmp(a->data, b->data, a->size) == 0;
}

ql_ret_t ql_str_find(const ql_str_t* str, const char* needle, size_t* out_index)
{
    size_t needle_length;
    size_t i;

    if (str == NULL || needle == NULL || out_index == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_str_find: str, needle or out_index is NULL");
        return QL_ERR_INVALID_ARG;
    }

    needle_length = strlen(needle);
    if (needle_length == 0)
    {
        *out_index = 0;
        return QL_OK;
    }
    if (str->size < needle_length)
    {
        return QL_ERR_NOT_FOUND;
    }

    /* memmem is a GNU extension, so scan by hand. */
    for (i = 0; i <= str->size - needle_length; i++)
    {
        if (memcmp(str->data + i, needle, needle_length) == 0)
        {
            *out_index = i;
            return QL_OK;
        }
    }

    return QL_ERR_NOT_FOUND;
}
