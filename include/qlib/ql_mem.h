#ifndef QLIB_QL_MEM_H
#define QLIB_QL_MEM_H

/**
 * @file ql_mem.h
 * @brief The replaceable allocator, and the overflow checked allocation
 *        helpers built on it.
 *
 * Every allocation in qlib goes through a ql_allocator_t supplied by the
 * caller. Passing NULL wherever one is expected selects the default,
 * libc backed allocator.
 */

#include <stddef.h>

#include "qlib/ql_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------------------------------------------------------------------
 * Allocator abstraction
 * ------------------------------------------------------------------------ */

/** @brief Allocation function prototype (see ql_allocator_t). */
typedef void* (*ql_malloc_fn)(size_t size, void* userdata);

/** @brief Reallocation function prototype (see ql_allocator_t). */
typedef void* (*ql_realloc_fn)(void* ptr, size_t size, void* userdata);

/** @brief Deallocation function prototype (see ql_allocator_t). */
typedef void (*ql_free_fn)(void* ptr, void* userdata);

/**
 * @brief Replaceable memory allocator.
 *
 * All three function pointers must be non-NULL; use ql_default_allocator() for
 * the libc backed implementation. Passing a NULL ql_allocator_t* to the helpers
 * below means "use the default allocator".
 *
 * The struct may be passed by value across the ABI, so its layout is fixed:
 * new members are only ever taken from @c reserved.
 */
typedef struct
{
    /** @brief Allocate @p size bytes, or return NULL. Must not be NULL. */
    ql_malloc_fn malloc;
    /** @brief Resize a block, following libc realloc semantics. Must not be NULL. */
    ql_realloc_fn realloc;
    /** @brief Release a block. Must tolerate a NULL pointer. Must not be NULL. */
    ql_free_fn free;
    /** @brief Opaque pointer handed back to every callback. May be NULL. */
    void* userdata;
    /** Reserved for future growth. Must be zeroed; do not use. */
    void* reserved[4];
} ql_allocator_t;

/**
 * @brief Allocator backed by libc malloc/realloc/free.
 *
 * @return A fully populated allocator with zeroed reserved members.
 */
QLIB_EXPORT ql_allocator_t ql_default_allocator(void);

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/**
 * @brief Multiply two sizes, rejecting overflow.
 *
 * @param count  Element count.
 * @param size   Element size.
 * @param result Receives @c count*size on success. Must not be NULL.
 * @return QL_OK, QL_ERR_INVALID_ARG if @p result is NULL, or QL_ERR_OVERFLOW.
 *         On failure @p result is left unmodified.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_size_mul_overflow(size_t count, size_t size, size_t* result);

/**
 * @brief Add two sizes, rejecting overflow.
 *
 * @param a      First addend.
 * @param b      Second addend.
 * @param result Receives @c a+b on success. Must not be NULL.
 * @return QL_OK, QL_ERR_INVALID_ARG if @p result is NULL, or QL_ERR_OVERFLOW.
 *         On failure @p result is left unmodified.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_size_add_overflow(size_t a, size_t b, size_t* result);

/**
 * @brief Allocate @p size bytes.
 *
 * @param alloc Allocator, or NULL for the default one.
 * @param size  Byte count. A size of 0 returns NULL without calling the
 *              allocator and without raising an error.
 * @return The block, or NULL on failure (QL_ERR_NOMEM is raised).
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_alloc(const ql_allocator_t* alloc, size_t size);

/**
 * @brief Allocate @p count * @p size bytes, zero filled.
 *
 * @return The block, or NULL on failure or overflow (QL_ERR_NOMEM or
 *         QL_ERR_OVERFLOW is raised).
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_calloc(const ql_allocator_t* alloc, size_t count, size_t size);

/**
 * @brief Resize a block.
 *
 * A @p size of 0 frees @p ptr and returns NULL. On failure the original block
 * is left untouched and still owned by the caller, exactly like libc realloc.
 *
 * @return The resized block, or NULL on failure (QL_ERR_NOMEM is raised).
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_realloc(const ql_allocator_t* alloc, void* ptr, size_t size);

/**
 * @brief Allocate @p count elements of @p size bytes, rejecting overflow.
 *
 * Prefer this over a hand written @c count*size: the multiplication is checked.
 *
 * @return The block, or NULL on failure or overflow.
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_alloc_array(const ql_allocator_t* alloc, size_t count, size_t size);

/**
 * @brief Resize an array allocation, rejecting overflow.
 *
 * @return The resized block, or NULL on failure or overflow. On failure the
 *         original block is left untouched.
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_realloc_array(const ql_allocator_t* alloc, void* ptr, size_t count, size_t size);

/**
 * @brief Release a block. Passing NULL is a no-op.
 */
QLIB_EXPORT void ql_dealloc(const ql_allocator_t* alloc, void* ptr);

/**
 * @brief Allocate @p size bytes aligned to @p alignment.
 *
 * @p alignment must be a non-zero power of two. Unlike C11 @c aligned_alloc,
 * @p size does **not** have to be a multiple of @p alignment.
 *
 * The block comes from @p alloc's own @c malloc, so a custom allocator stays in
 * control of the memory: it is over-allocated by @c alignment+sizeof(void*) and
 * aligned by hand, which is the per-block overhead. The original pointer is
 * recorded immediately before the returned address.
 *
 * Free the result with ql_aligned_free(), never with ql_dealloc() or libc
 * @c free: the pointer handed back is not the one the allocator returned. There
 * is no aligned realloc; allocate, copy and free instead.
 *
 * @param alloc     Allocator, or NULL for the default one.
 * @param alignment Power-of-two alignment in bytes.
 * @param size      Byte count. A size of 0 returns NULL without calling the
 *                  allocator and without raising an error.
 * @return The aligned block, or NULL on failure. QL_ERR_INVALID_ARG is raised
 *         for a bad alignment, QL_ERR_OVERFLOW if the padded size cannot be
 *         represented, and QL_ERR_NOMEM if the allocation fails.
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_aligned_alloc(const ql_allocator_t* alloc, size_t alignment, size_t size);

/**
 * @brief Release a block from ql_aligned_alloc(). Passing NULL is a no-op.
 *
 * @p alloc must be the same allocator the block was allocated with.
 */
QLIB_EXPORT void ql_aligned_free(const ql_allocator_t* alloc, void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_MEM_H */
