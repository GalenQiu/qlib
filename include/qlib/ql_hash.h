#ifndef QLIB_QL_HASH_H
#define QLIB_QL_HASH_H

/**
 * @file ql_hash.h
 * @brief A hash table mapping fixed size keys to fixed size values.
 *
 * Keys and values are raw byte blocks of a size fixed at creation, the same
 * idea as `ql_vector`'s `elem_size`. Both are copied in and out, so the caller's
 * memory is never retained and nothing has to be freed by hand.
 *
 * Keys are compared with `memcmp` over the whole key, which means:
 *
 *   - an integer key works, but must be fully initialised -- padding bytes in a
 *     struct key are part of the key and will make otherwise equal keys differ;
 *   - a C string key works, but the key size then fixes the maximum length and
 *     short strings waste space. Hash a string's bytes with a length that the
 *     caller controls, or store a pointer and hash its contents yourself.
 *
 * A `QLIB_HASH_KEY_IS_POD` habit of putting a struct with `reserved[]` members
 * in as a key will produce a table that never finds anything, because the
 * reserved bytes differ.
 *
 * Lookup is amortised O(1): open addressing with linear probing over a
 * power-of-two slot array. Nothing in the API promises an iteration order, and
 * removal does not preserve it.
 */

#include <stdbool.h>
#include <stddef.h>

#include "qlib/ql_common.h"
#include "qlib/ql_mem.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief An opaque hash table. */
typedef struct ql_hash ql_hash_t;

/**
 * @brief Iterator state for walking a table.
 *
 * A value type so that walking a table does not allocate. Initialise with
 * ql_hash_iter_init() and do not touch the members.
 */
typedef struct
{
    /** @brief The table being walked. */
    const ql_hash_t* hash;
    /** @brief Current slot position. Internal. */
    size_t slot;
    /** @brief Reserved for future growth. Must be zeroed; do not use. */
    void* reserved[2];
} ql_hash_iter_t;

/**
 * @brief Create a table.
 *
 * @param alloc      Allocator, or NULL for the default one.
 * @param key_size   Bytes per key. Must be at least 1.
 * @param value_size Bytes per value. Zero is allowed, which makes the table a
 *                   membership set: ql_hash_set() then takes a NULL @p value
 *                   and ql_hash_get() takes a NULL @p out_value.
 * @param out        Receives the table. Set to NULL before any failure.
 * @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_hash_create(const ql_allocator_t* alloc, size_t key_size, size_t value_size, ql_hash_t** out);

/** @brief Destroy a table and free its storage. Passing NULL is a no-op. */
QLIB_EXPORT void ql_hash_destroy(ql_hash_t* hash);

/**
 * @brief Insert a key, or overwrite the value of an existing one.
 *
 * @param hash  Table.
 * @param key   Key to copy in. Must not be NULL.
 * @param value Value to copy in. Must not be NULL unless the table was created
 *              with `value_size == 0`.
 * @return QL_OK, QL_ERR_INVALID_ARG, QL_ERR_NOMEM or QL_ERR_OVERFLOW.
 *         On failure the table is unchanged.
 */
QLIB_EXPORT ql_ret_t ql_hash_set(ql_hash_t* hash, const void* key, const void* value);

/**
 * @brief Look a key up.
 *
 * @param hash      Table.
 * @param key       Key to find. Must not be NULL.
 * @param out_value Receives the value. May be NULL only when `value_size == 0`.
 * @return QL_OK when found, QL_ERR_NOT_FOUND when not, QL_ERR_INVALID_ARG for a
 *         bad argument. On QL_ERR_NOT_FOUND @p out_value is left untouched.
 */
QLIB_EXPORT ql_ret_t ql_hash_get(const ql_hash_t* hash, const void* key, void* out_value);

/** @brief Whether a key is present. False for a NULL table or key. */
QLIB_NODISCARD QLIB_EXPORT bool ql_hash_contains(const ql_hash_t* hash, const void* key);

/**
 * @brief Remove a key and its value.
 *
 * @return QL_OK, QL_ERR_NOT_FOUND if the key is absent, or QL_ERR_INVALID_ARG.
 */
QLIB_EXPORT ql_ret_t ql_hash_remove(ql_hash_t* hash, const void* key);

/**
 * @brief Remove every entry, keeping the allocation.
 *
 * Passing NULL is a no-op. This never fails.
 */
QLIB_EXPORT void ql_hash_clear(ql_hash_t* hash);

/** @brief Number of entries. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_hash_size(const ql_hash_t* hash);

/** @brief Slots currently allocated. Not a limit on the entry count. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_hash_capacity(const ql_hash_t* hash);

/** @brief Key size the table was created with. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_hash_key_size(const ql_hash_t* hash);

/** @brief Value size the table was created with. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_hash_value_size(const ql_hash_t* hash);

/**
 * @brief Start walking a table.
 *
 * @param it   Iterator to initialise.
 * @param hash Table to walk. May be NULL, in which case the first
 *             ql_hash_iter_next() call returns false.
 */
QLIB_EXPORT void ql_hash_iter_init(ql_hash_iter_t* it, const ql_hash_t* hash);

/**
 * @brief Advance to the next entry.
 *
 * @param it         Iterator initialised by ql_hash_iter_init().
 * @param out_key    Receives a pointer to the key. May be NULL if the key is
 *                   not wanted. Valid until the table is next modified.
 * @param out_value  Receives a pointer to the value, or NULL when
 *                   `value_size == 0`. Valid until the table is next modified.
 * @return true when an entry was produced, false at the end.
 *
 * Removing from the table invalidates both the pointers and the iterator's
 * position; start again with ql_hash_iter_init() after any modification.
 */
QLIB_EXPORT bool ql_hash_iter_next(ql_hash_iter_t* it, const void** out_key, const void** out_value);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_HASH_H */
