#ifndef QLIB_QL_STR_H
#define QLIB_QL_STR_H

/**
 * @file ql_str.h
 * @brief A growable byte string that is always NUL terminated.
 *
 * `ql_str_t` owns a buffer of bytes plus a length. Two guarantees make it
 * pleasant to use as a builder:
 *
 *   - `ql_str_cstr()` always returns a valid C string, even on an empty string,
 *     so the result can be handed straight to `printf`, `fopen` or any C API.
 *   - Every mutating call leaves the string in a consistent state: it either
 *     succeeds, or it fails and the previous contents are untouched. There is no
 *     state where the length and the buffer disagree.
 *
 * The length is authoritative. `ql_str_append_n()` accepts data containing NUL
 * bytes, so a string may hold embedded NULs; after that `ql_str_size()` is
 * larger than `strlen(ql_str_cstr())`. Nothing in the API breaks, but anything
 * that treats the result as a C string will stop at the first NUL, which is
 * usually the intent when the data came from a C API in the first place.
 *
 * All allocation goes through a `ql_allocator_t`, as everywhere else in qlib. A
 * `NULL` allocator means the default one.
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

/** @brief An opaque growable string. */
typedef struct ql_str ql_str_t;

/**
 * @brief Create an empty string.
 *
 * @param alloc Allocator, or NULL for the default one.
 * @param out   Receives the new string. Set to NULL before any failure.
 * @return QL_OK, or QL_ERR_INVALID_ARG if @p out is NULL, or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_str_create(const ql_allocator_t* alloc, ql_str_t** out);

/**
 * @brief Create a string holding a copy of a C string.
 *
 * @param alloc Allocator, or NULL for the default one.
 * @param cstr  NUL terminated source. Must not be NULL.
 * @param out   Receives the new string. Set to NULL before any failure.
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_str_create_from(const ql_allocator_t* alloc, const char* cstr, ql_str_t** out);

/**
 * @brief Destroy a string and release its buffer.
 *
 * Passing NULL is a no-op, so a destroy can follow a failed create.
 */
QLIB_EXPORT void ql_str_destroy(ql_str_t* str);

/**
 * @brief Replace the contents with a copy of a C string.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, or QL_ERR_NOMEM. On failure the previous
 *         contents are intact.
 */
QLIB_EXPORT ql_ret_t ql_str_assign(ql_str_t* str, const char* cstr);

/**
 * @brief Replace the contents with @p len bytes from @p data.
 *
 * @p data may contain NUL bytes. A NULL @p data is only accepted when
 * @p len is 0.
 */
QLIB_EXPORT ql_ret_t ql_str_assign_n(ql_str_t* str, const char* data, size_t len);

/** @brief Append a C string. @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM. */
QLIB_EXPORT ql_ret_t ql_str_append(ql_str_t* str, const char* cstr);

/** @brief Append @p len bytes from @p data; may contain NUL bytes. */
QLIB_EXPORT ql_ret_t ql_str_append_n(ql_str_t* str, const char* data, size_t len);

/** @brief Append a single byte, NUL included. */
QLIB_EXPORT ql_ret_t ql_str_append_char(ql_str_t* str, char c);

/** @brief Append a whole string, including any embedded NUL bytes it holds. */
QLIB_EXPORT ql_ret_t ql_str_append_str(ql_str_t* str, const ql_str_t* other);

/**
 * @brief Append formatted text.
 *
 * Uses `vsnprintf` and grows the buffer until the result fits, so a format that
 * expands to a large string is handled rather than truncated. The format is
 * applied to a temporary copy of @p ap, so the caller's `va_list` is not
 * consumed by a failed attempt.
 *
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_NOMEM, or QL_ERR_OVERFLOW if the
 *         formatted result would exceed the maximum representable size.
 */
QLIB_EXPORT ql_ret_t ql_str_append_vprintf(ql_str_t* str, const char* fmt, va_list ap);

/**
 * @brief Append formatted text. Convenience wrapper over
 *        ql_str_append_vprintf().
 */
QLIB_EXPORT ql_ret_t ql_str_append_printf(ql_str_t* str, const char* fmt, ...);

/**
 * @brief Ensure the buffer can hold @p capacity bytes plus a terminator.
 *
 * Never shrinks. @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_str_reserve(ql_str_t* str, size_t capacity);

/**
 * @brief Make the string empty without releasing its buffer.
 *
 * Passing NULL is a no-op. This never fails.
 */
QLIB_EXPORT void ql_str_clear(ql_str_t* str);

/**
 * @brief The contents as a NUL terminated C string.
 *
 * Never NULL for a valid string; an empty string yields `""`. NULL is returned
 * for a NULL @p str.
 */
QLIB_NODISCARD QLIB_EXPORT const char* ql_str_cstr(const ql_str_t* str);

/** @brief Length in bytes, excluding the terminator. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_str_size(const ql_str_t* str);

/** @brief Bytes currently allocated, excluding the terminator. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_str_capacity(const ql_str_t* str);

/** @brief True when the string is empty or NULL. */
QLIB_NODISCARD QLIB_EXPORT bool ql_str_empty(const ql_str_t* str);

/**
 * @brief Order two strings the way `strcmp` would.
 *
 * Compares byte by byte over the shorter length, then treats the shorter string
 * as the lesser one. This is the same order as `strcmp` for strings without
 * embedded NULs, and a total order in all cases. NULL is treated as empty.
 */
QLIB_NODISCARD QLIB_EXPORT int ql_str_compare(const ql_str_t* a, const ql_str_t* b);

/**
 * @brief Compare against a C string for equality.
 *
 * Compares sizes first, so a string holding embedded NULs never equals a C
 * string of the same visible prefix.
 */
QLIB_NODISCARD QLIB_EXPORT bool ql_str_equal_cstr(const ql_str_t* str, const char* cstr);

/** @brief Compare two strings for equality, embedded NULs included. */
QLIB_NODISCARD QLIB_EXPORT bool ql_str_equal(const ql_str_t* a, const ql_str_t* b);

/**
 * @brief Find the first occurrence of a C string.
 *
 * @param str      String to search.
 * @param needle   Text to find. An empty needle matches at index 0.
 * @param out_index Receives the byte offset of the match.
 * @return QL_OK when found, QL_ERR_NOT_FOUND when not, QL_ERR_INVALID_ARG for a
 *         NULL argument. On QL_ERR_NOT_FOUND @p out_index is left untouched.
 */
QLIB_EXPORT ql_ret_t ql_str_find(const ql_str_t* str, const char* needle, size_t* out_index);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_STR_H */
