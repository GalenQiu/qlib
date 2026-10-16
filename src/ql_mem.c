#include "qlib/ql_mem.h"

#include "ql_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void* ql_def_malloc(size_t size, void* userdata)
{
    (void)userdata;
    return malloc(size);
}

static void* ql_def_realloc(void* ptr, size_t size, void* userdata)
{
    (void)userdata;
    return realloc(ptr, size);
}

static void ql_def_free(void* ptr, void* userdata)
{
    (void)userdata;
    free(ptr);
}

static const ql_allocator_t ql_default_alloc = {
    .malloc = ql_def_malloc,
    .realloc = ql_def_realloc,
    .free = ql_def_free,
    .userdata = NULL,
};

ql_allocator_t ql_default_allocator(void)
{
    return ql_default_alloc;
}

/**
 * @brief NULL means "use the default allocator".
 *
 * The all-three-members-non-NULL requirement is documented in ql_mem.h and is
 * not re-checked here: this is the allocation hot path.
 */
static const ql_allocator_t* ql_alloc_resolve(const ql_allocator_t* alloc)
{
    return alloc != NULL ? alloc : &ql_default_alloc;
}

ql_ret_t ql_size_mul_overflow(size_t count, size_t size, size_t* result)
{
    if (result == NULL)
    {
        return QL_ERR_INVALID_ARG;
    }
    if (count != 0 && size > SIZE_MAX / count)
    {
        return QL_ERR_OVERFLOW;
    }
    *result = count * size;
    return QL_OK;
}

ql_ret_t ql_size_add_overflow(size_t a, size_t b, size_t* result)
{
    if (result == NULL)
    {
        return QL_ERR_INVALID_ARG;
    }
    if (a > SIZE_MAX - b)
    {
        return QL_ERR_OVERFLOW;
    }
    *result = a + b;
    return QL_OK;
}

/** @brief Is @p value a non-zero power of two? */
static bool ql_is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

void* ql_alloc(const ql_allocator_t* alloc, size_t size)
{
    const ql_allocator_t* resolved = ql_alloc_resolve(alloc);
    void* ptr;

    if (size == 0)
    {
        return NULL;
    }

    ptr = resolved->malloc(size, resolved->userdata);
    if (ptr == NULL)
    {
        ql_err_raise(QL_ERR_NOMEM, "ql_alloc: allocation failed");
    }
    return ptr;
}

void* ql_calloc(const ql_allocator_t* alloc, size_t count, size_t size)
{
    size_t total = 0;
    void* ptr;

    if (ql_size_mul_overflow(count, size, &total) != QL_OK)
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_calloc: element count overflows size_t");
        return NULL;
    }

    ptr = ql_alloc(alloc, total);
    if (ptr != NULL)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* ql_realloc(const ql_allocator_t* alloc, void* ptr, size_t size)
{
    const ql_allocator_t* resolved = ql_alloc_resolve(alloc);
    void* out;

    if (size == 0)
    {
        resolved->free(ptr, resolved->userdata);
        return NULL;
    }

    out = resolved->realloc(ptr, size, resolved->userdata);
    if (out == NULL)
    {
        /* ptr is still valid and still owned by the caller, as with libc. */
        ql_err_raise(QL_ERR_NOMEM, "ql_realloc: reallocation failed");
    }
    return out;
}

void* ql_alloc_array(const ql_allocator_t* alloc, size_t count, size_t size)
{
    size_t total = 0;

    if (ql_size_mul_overflow(count, size, &total) != QL_OK)
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_alloc_array: element count overflows size_t");
        return NULL;
    }
    return ql_alloc(alloc, total);
}

void* ql_realloc_array(const ql_allocator_t* alloc, void* ptr, size_t count, size_t size)
{
    size_t total = 0;

    if (ql_size_mul_overflow(count, size, &total) != QL_OK)
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_realloc_array: element count overflows size_t");
        return NULL;
    }
    return ql_realloc(alloc, ptr, total);
}

void ql_dealloc(const ql_allocator_t* alloc, void* ptr)
{
    const ql_allocator_t* resolved;

    if (ptr == NULL)
    {
        return;
    }

    resolved = ql_alloc_resolve(alloc);
    resolved->free(ptr, resolved->userdata);
}

void* ql_aligned_alloc(const ql_allocator_t* alloc, size_t alignment, size_t size)
{
    const ql_allocator_t* resolved = ql_alloc_resolve(alloc);
    size_t overhead = 0;
    size_t total = 0;
    size_t padding;
    uintptr_t mask;
    unsigned char* raw;
    unsigned char* base;
    unsigned char* aligned;

    if (!ql_is_power_of_two(alignment))
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_aligned_alloc: alignment must be a non-zero power of two");
        return NULL;
    }
    if (size == 0)
    {
        return NULL;
    }

    /*
     * Room for the caller's bytes, enough slack to align up inside the block,
     * and sizeof(void*) immediately before the aligned address to remember the
     * pointer the allocator actually handed back.
     */
    if (ql_size_add_overflow(alignment, sizeof(void*), &overhead) != QL_OK ||
        ql_size_add_overflow(size, overhead, &total) != QL_OK)
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_aligned_alloc: size + alignment overflows size_t");
        return NULL;
    }

    raw = resolved->malloc(total, resolved->userdata);
    if (raw == NULL)
    {
        ql_err_raise(QL_ERR_NOMEM, "ql_aligned_alloc: allocation failed");
        return NULL;
    }

    /*
     * Skip sizeof(void*) first so that there is always room for the header at
     * aligned - sizeof(void*) even when alignment is 1. Both that header slot
     * and the returned address end up correctly aligned for a void*, because
     * malloc returns memory aligned for any type and alignment is a power of
     * two.
     *
     * The rounding is done on the address as an integer, then applied back as
     * pointer arithmetic, so no integer is ever cast to a pointer.
     */
    base = raw + sizeof(void*);
    mask = (uintptr_t)alignment - 1U;
    padding = (size_t)(((uintptr_t)alignment - ((uintptr_t)base & mask)) & mask);
    aligned = base + padding;

    ((void**)aligned)[-1] = raw;

    return aligned;
}

void ql_aligned_free(const ql_allocator_t* alloc, void* ptr)
{
    const ql_allocator_t* resolved;

    if (ptr == NULL)
    {
        return;
    }

    resolved = ql_alloc_resolve(alloc);
    resolved->free(((void**)ptr)[-1], resolved->userdata);
}
