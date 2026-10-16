#include "qlib/ql_vector.h"

#include "ql_internal.h"

#include <stdint.h>
#include <string.h>

/* Capacity of a vector that has not grown yet. */
#define QL_VECTOR_MIN_CAPACITY 8

struct ql_vector
{
    ql_allocator_t alloc;
    void* data;
    size_t size;
    size_t capacity;
    size_t elem_size;
};

/** @brief Byte offset of element @p index. */
static size_t ql_vector_offset(const ql_vector_t* vec, size_t index)
{
    return index * vec->elem_size;
}

/**
 * @brief Does @p p point into the live element buffer?
 *
 * Used to detect calls such as ql_vector_push_back(vec, ql_vector_at(vec, 0)).
 * Without this, growing the buffer would realloc the very memory being read,
 * leaving @p elem dangling.
 */
static bool ql_vector_contains(const ql_vector_t* vec, const void* p)
{
    const char* base = (const char*)vec->data;
    const char* end;

    if (base == NULL)
    {
        return false;
    }
    end = base + ql_vector_offset(vec, vec->size);
    return (const char*)p >= base && (const char*)p < end;
}

/**
 * @brief Make room for at least @p min_capacity elements.
 *
 * Doubles until it is big enough. The vector is untouched on failure, so a
 * failed grow never loses the existing elements.
 */
static ql_ret_t ql_vector_grow(ql_vector_t* vec, size_t min_capacity)
{
    size_t new_capacity = vec->capacity != 0 ? vec->capacity : QL_VECTOR_MIN_CAPACITY;
    size_t bytes = 0;
    void* new_data;

    while (new_capacity < min_capacity)
    {
        if (new_capacity > SIZE_MAX / 2)
        {
            /* Doubling would wrap; ask for exactly what is needed. */
            new_capacity = min_capacity;
            break;
        }
        new_capacity *= 2;
    }

    if (ql_size_mul_overflow(new_capacity, vec->elem_size, &bytes) != QL_OK)
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_vector: capacity * elem_size overflows size_t");
        return QL_ERR_OVERFLOW;
    }

    new_data = ql_realloc(&vec->alloc, vec->data, bytes);
    if (new_data == NULL)
    {
        /* ql_realloc already raised QL_ERR_NOMEM; data is still intact. */
        return QL_ERR_NOMEM;
    }

    vec->data = new_data;
    vec->capacity = new_capacity;
    return QL_OK;
}

/**
 * @brief Copy @p elem somewhere stable if it aliases the vector's own buffer.
 *
 * @param scratch Receives a temporary copy, or NULL when no copy was needed.
 */
static ql_ret_t ql_vector_pin_elem(ql_vector_t* vec, const void* elem, const void** pinned, void** scratch)
{
    *pinned = elem;
    *scratch = NULL;

    if (!ql_vector_contains(vec, elem))
    {
        return QL_OK;
    }

    *scratch = ql_alloc(&vec->alloc, vec->elem_size);
    if (*scratch == NULL)
    {
        return QL_ERR_NOMEM;
    }
    memcpy(*scratch, elem, vec->elem_size);
    *pinned = *scratch;
    return QL_OK;
}

ql_ret_t ql_vector_create(const ql_allocator_t* alloc, size_t elem_size, ql_vector_t** out)
{
    ql_vector_t* vec;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_create: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    if (elem_size == 0)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_create: elem_size is 0");
        return QL_ERR_INVALID_ARG;
    }

    vec = ql_alloc(alloc, sizeof *vec);
    if (vec == NULL)
    {
        return QL_ERR_NOMEM;
    }

    vec->alloc = alloc != NULL ? *alloc : ql_default_allocator();
    vec->data = NULL;
    vec->size = 0;
    vec->capacity = 0;
    vec->elem_size = elem_size;

    *out = vec;
    return QL_OK;
}

void ql_vector_destroy(ql_vector_t* vec)
{
    ql_allocator_t alloc;

    if (vec == NULL)
    {
        return;
    }

    /* Copy the allocator out before the control block itself goes away. */
    alloc = vec->alloc;
    ql_dealloc(&alloc, vec->data);
    ql_dealloc(&alloc, vec);
}

void ql_vector_clear(ql_vector_t* vec)
{
    if (vec != NULL)
    {
        vec->size = 0;
    }
}

ql_ret_t ql_vector_reserve(ql_vector_t* vec, size_t capacity)
{
    if (vec == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_reserve: vec is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (capacity <= vec->capacity)
    {
        return QL_OK;
    }
    return ql_vector_grow(vec, capacity);
}

ql_ret_t ql_vector_resize(ql_vector_t* vec, size_t count)
{
    if (vec == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_resize: vec is NULL");
        return QL_ERR_INVALID_ARG;
    }

    if (count > vec->size)
    {
        ql_ret_t rc = ql_vector_reserve(vec, count);
        if (rc != QL_OK)
        {
            return rc;
        }
        memset((char*)vec->data + ql_vector_offset(vec, vec->size), 0, ql_vector_offset(vec, count - vec->size));
    }

    vec->size = count;
    return QL_OK;
}

ql_ret_t ql_vector_push_back(ql_vector_t* vec, const void* elem)
{
    const void* pinned = NULL;
    void* scratch = NULL;
    ql_ret_t rc;

    if (vec == NULL || elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_push_back: vec or elem is NULL");
        return QL_ERR_INVALID_ARG;
    }

    rc = ql_vector_pin_elem(vec, elem, &pinned, &scratch);
    if (rc == QL_OK && vec->size == vec->capacity)
    {
        rc = ql_vector_grow(vec, vec->size + 1);
    }
    if (rc != QL_OK)
    {
        ql_dealloc(&vec->alloc, scratch);
        return rc;
    }

    memcpy((char*)vec->data + ql_vector_offset(vec, vec->size), pinned, vec->elem_size);
    vec->size++;
    ql_dealloc(&vec->alloc, scratch);
    return QL_OK;
}

ql_ret_t ql_vector_pop_back(ql_vector_t* vec, void* out)
{
    if (vec == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_pop_back: vec is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (vec->size == 0)
    {
        ql_err_raise(QL_ERR_EMPTY, "ql_vector_pop_back: vector is empty");
        return QL_ERR_EMPTY;
    }

    vec->size--;
    if (out != NULL)
    {
        memcpy(out, (char*)vec->data + ql_vector_offset(vec, vec->size), vec->elem_size);
    }
    return QL_OK;
}

ql_ret_t ql_vector_insert(ql_vector_t* vec, size_t index, const void* elem)
{
    const void* pinned = NULL;
    void* scratch = NULL;
    ql_ret_t rc;

    if (vec == NULL || elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_insert: vec or elem is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (index > vec->size)
    {
        ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_vector_insert: index is past the end");
        return QL_ERR_OUT_OF_RANGE;
    }

    rc = ql_vector_pin_elem(vec, elem, &pinned, &scratch);
    if (rc == QL_OK && vec->size == vec->capacity)
    {
        rc = ql_vector_grow(vec, vec->size + 1);
    }
    if (rc != QL_OK)
    {
        ql_dealloc(&vec->alloc, scratch);
        return rc;
    }

    /* Shift [index, size) one slot to the right before writing the new value. */
    memmove((char*)vec->data + ql_vector_offset(vec, index + 1), (char*)vec->data + ql_vector_offset(vec, index),
            ql_vector_offset(vec, vec->size - index));
    memcpy((char*)vec->data + ql_vector_offset(vec, index), pinned, vec->elem_size);

    vec->size++;
    ql_dealloc(&vec->alloc, scratch);
    return QL_OK;
}

ql_ret_t ql_vector_remove(ql_vector_t* vec, size_t index)
{
    if (vec == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_remove: vec is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (index >= vec->size)
    {
        ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_vector_remove: index is out of range");
        return QL_ERR_OUT_OF_RANGE;
    }

    memmove((char*)vec->data + ql_vector_offset(vec, index), (char*)vec->data + ql_vector_offset(vec, index + 1),
            ql_vector_offset(vec, vec->size - index - 1));
    vec->size--;
    return QL_OK;
}

ql_ret_t ql_vector_assign(ql_vector_t* vec, size_t index, const void* elem)
{
    if (vec == NULL || elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_vector_assign: vec or elem is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (index >= vec->size)
    {
        ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_vector_assign: index is out of range");
        return QL_ERR_OUT_OF_RANGE;
    }

    /* memmove, not memcpy: elem may be another element of this same vector. */
    memmove((char*)vec->data + ql_vector_offset(vec, index), elem, vec->elem_size);
    return QL_OK;
}

void* ql_vector_at(ql_vector_t* vec, size_t index)
{
    if (vec == NULL || index >= vec->size)
    {
        return NULL;
    }
    return (char*)vec->data + ql_vector_offset(vec, index);
}

void* ql_vector_data(ql_vector_t* vec)
{
    return vec != NULL ? vec->data : NULL;
}

size_t ql_vector_size(const ql_vector_t* vec)
{
    return vec != NULL ? vec->size : 0;
}

size_t ql_vector_capacity(const ql_vector_t* vec)
{
    return vec != NULL ? vec->capacity : 0;
}

size_t ql_vector_elem_size(const ql_vector_t* vec)
{
    return vec != NULL ? vec->elem_size : 0;
}

bool ql_vector_empty(const ql_vector_t* vec)
{
    return vec == NULL || vec->size == 0;
}
