#ifndef QLIB_QL_VECTOR_H
#define QLIB_QL_VECTOR_H

#include <stdbool.h>
#include <stddef.h>

#include "qlib/ql_common.h"
#include "qlib/ql_mem.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Contiguous, growable array of fixed size elements.
 *
 * Elements are copied in and out bytewise with memcpy, so element types must be
 * trivially copyable. The type is opaque: its layout is private to the library
 * and may change without breaking the ABI.
 *
 * Pointers handed out by ql_vector_at() and ql_vector_data() are invalidated by
 * any operation that can change the capacity, and by inserts and removes before
 * the element. Re-fetch them after mutating.
 */
typedef struct ql_vector ql_vector_t;

/**
 * @brief Create an empty vector.
 *
 * The allocator is copied into the vector, so the caller's struct does not need
 * to outlive the call.
 *
 * @param alloc     Allocator, or NULL for the default one.
 * @param elem_size Size of one element in bytes. Must be non-zero.
 * @param out       Receives the new vector. Set to NULL on failure.
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_NOMEM.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_create(const ql_allocator_t* alloc, size_t elem_size, ql_vector_t** out);

/**
 * @brief Release a vector and all of its storage. Passing NULL is a no-op.
 */
QLIB_EXPORT void ql_vector_destroy(ql_vector_t* vec);

/**
 * @brief Drop every element. Capacity is retained.
 */
QLIB_EXPORT void ql_vector_clear(ql_vector_t* vec);

/**
 * @brief Ensure room for at least @p capacity elements.
 *
 * Never shrinks. @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_OVERFLOW, or
 * QL_ERR_NOMEM. On failure the vector is left unchanged.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_reserve(ql_vector_t* vec, size_t capacity);

/**
 * @brief Grow or shrink the element count.
 *
 * Newly exposed elements are zero filled. Shrinking only lowers the count; the
 * bytes stay in the buffer until they are overwritten.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_OVERFLOW, or QL_ERR_NOMEM.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_resize(ql_vector_t* vec, size_t count);

/**
 * @brief Append a copy of @p elem.
 *
 * @p elem may point into @p vec itself; that case is detected and handled.
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_OVERFLOW, or QL_ERR_NOMEM.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_push_back(ql_vector_t* vec, const void* elem);

/**
 * @brief Remove the last element.
 *
 * @param out Receives a copy of the element, or NULL to discard it.
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_EMPTY.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_pop_back(ql_vector_t* vec, void* out);

/**
 * @brief Insert a copy of @p elem so that it ends up at @p index.
 *
 * @p index may equal the current size, which appends. @p elem may point into
 * @p vec itself.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_OUT_OF_RANGE, QL_ERR_OVERFLOW, or
 *         QL_ERR_NOMEM.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_insert(ql_vector_t* vec, size_t index, const void* elem);

/**
 * @brief Remove the element at @p index, shifting the rest down.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_OUT_OF_RANGE.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_remove(ql_vector_t* vec, size_t index);

/**
 * @brief Overwrite the element at @p index.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_OUT_OF_RANGE.
 */
QLIB_NODISCARD QLIB_EXPORT ql_ret_t ql_vector_assign(ql_vector_t* vec, size_t index, const void* elem);

/**
 * @brief Pointer to the element at @p index, or NULL if out of range.
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_vector_at(ql_vector_t* vec, size_t index);

/**
 * @brief Pointer to the first element, or NULL when empty.
 */
QLIB_NODISCARD QLIB_EXPORT void* ql_vector_data(ql_vector_t* vec);

/** @brief Number of elements. Returns 0 for a NULL vector. */
QLIB_EXPORT size_t ql_vector_size(const ql_vector_t* vec);

/** @brief Allocated element slots. Returns 0 for a NULL vector. */
QLIB_EXPORT size_t ql_vector_capacity(const ql_vector_t* vec);

/** @brief Element size in bytes. Returns 0 for a NULL vector. */
QLIB_EXPORT size_t ql_vector_elem_size(const ql_vector_t* vec);

/** @brief True when the vector holds no elements. A NULL vector counts as empty. */
QLIB_EXPORT bool ql_vector_empty(const ql_vector_t* vec);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_VECTOR_H */
