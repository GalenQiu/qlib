#ifndef QLIB_QL_LIST_H
#define QLIB_QL_LIST_H

/**
 * @file ql_list.h
 * @brief A doubly linked list of fixed size elements.
 *
 * `ql_vector` covers most needs and beats a list at everything except two:
 * inserting or removing in the middle in constant time, and keeping a reference
 * to an element that survives other elements coming and going.
 *
 * Both need a handle, and that is what ql_list_node_t is. A node stays valid
 * until it is removed -- or until the list is cleared or destroyed -- no matter
 * how many other nodes are added or removed around it, and removing it costs
 * nothing beyond unlinking it. Pushing and inserting hand the node back so the
 * caller can keep it.
 *
 * Elements are copied in and out, never referenced. No pointer into the stored
 * element is ever handed out, so the alignment of the element type does not
 * matter and a caller cannot accidentally keep a reference that a removal
 * invalidates.
 *
 * There is no iterator type: walk with ql_list_first() and ql_list_next(), or
 * backwards with ql_list_last() and ql_list_prev().
 */

#include <stdbool.h>
#include <stddef.h>

#include "qlib/ql_common.h"
#include "qlib/ql_mem.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** @brief An opaque list. */
typedef struct ql_list ql_list_t;

/** @brief An opaque node, valid until it is removed from its list. */
typedef struct ql_list_node ql_list_node_t;

/**
 * @brief Create an empty list.
 *
 * @param alloc     Allocator, or NULL for the default one.
 * @param elem_size Bytes per element. Must be at least 1.
 * @param out       Receives the list. Set to NULL before any failure.
 * @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_list_create(const ql_allocator_t* alloc, size_t elem_size, ql_list_t** out);

/** @brief Destroy a list and every node in it. Passing NULL is a no-op. */
QLIB_EXPORT void ql_list_destroy(ql_list_t* list);

/**
 * @brief Insert a copy of an element at the front.
 *
 * @param list     List.
 * @param elem     Element to copy in. Must not be NULL.
 * @param out_node Receives the new node, or may be NULL if it is not wanted.
 * @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_list_push_front(ql_list_t* list, const void* elem, ql_list_node_t** out_node);

/** @brief Insert a copy of an element at the back. See ql_list_push_front(). */
QLIB_EXPORT ql_ret_t ql_list_push_back(ql_list_t* list, const void* elem, ql_list_node_t** out_node);

/**
 * @brief Insert a copy of an element before an existing node.
 *
 * @param list     List that @p node belongs to.
 * @param node     Node to insert before. Must not be NULL; use
 *                 ql_list_push_back() to append.
 * @param elem     Element to copy in. Must not be NULL.
 * @param out_node Receives the new node, or may be NULL.
 * @return QL_OK, QL_ERR_INVALID_ARG or QL_ERR_NOMEM.
 */
QLIB_EXPORT ql_ret_t ql_list_insert_before(ql_list_t* list, ql_list_node_t* node, const void* elem,
                                           ql_list_node_t** out_node);

/** @brief Insert a copy of an element after an existing node. */
QLIB_EXPORT ql_ret_t ql_list_insert_after(ql_list_t* list, ql_list_node_t* node, const void* elem,
                                          ql_list_node_t** out_node);

/**
 * @brief Remove a node and free it.
 *
 * @param list List that @p node belongs to.
 * @param node Node to remove. Must belong to @p list.
 * @return QL_OK, or QL_ERR_INVALID_ARG if either argument is NULL.
 *
 * The node is invalid afterwards; no other node is affected.
 */
QLIB_EXPORT ql_ret_t ql_list_remove(ql_list_t* list, ql_list_node_t* node);

/**
 * @brief Remove the front element and copy it out.
 *
 * @param list     List.
 * @param out_elem Receives the element, or may be NULL to just discard it.
 * @return QL_OK, or QL_ERR_NOT_FOUND if the list is empty.
 */
QLIB_EXPORT ql_ret_t ql_list_pop_front(ql_list_t* list, void* out_elem);

/** @brief Remove the back element and copy it out. */
QLIB_EXPORT ql_ret_t ql_list_pop_back(ql_list_t* list, void* out_elem);

/**
 * @brief Copy the front element out without removing it.
 *
 * @return QL_OK, QL_ERR_NOT_FOUND if the list is empty, or QL_ERR_INVALID_ARG.
 */
QLIB_EXPORT ql_ret_t ql_list_front(const ql_list_t* list, void* out_elem);

/** @brief Copy the back element out without removing it. */
QLIB_EXPORT ql_ret_t ql_list_back(const ql_list_t* list, void* out_elem);

/**
 * @brief Copy a node's element out.
 *
 * @param list     The list the node belongs to; it is what knows the element
 *                 size, so it is required even though the node is what gets
 *                 read.
 * @param node     Node to read.
 * @param out_elem Receives the element.
 * @return QL_OK, or QL_ERR_INVALID_ARG if any argument is NULL.
 */
QLIB_EXPORT ql_ret_t ql_list_node_value(const ql_list_t* list, const ql_list_node_t* node, void* out_elem);

/**
 * @brief The first node, or NULL when the list is empty or NULL.
 *
 * The node stays valid until it is removed or the list is cleared or destroyed.
 */
QLIB_NODISCARD QLIB_EXPORT ql_list_node_t* ql_list_first(const ql_list_t* list);

/** @brief The last node, or NULL when the list is empty or NULL. */
QLIB_NODISCARD QLIB_EXPORT ql_list_node_t* ql_list_last(const ql_list_t* list);

/** @brief The next node, or NULL at the end or for a NULL node. */
QLIB_NODISCARD QLIB_EXPORT ql_list_node_t* ql_list_next(const ql_list_node_t* node);

/** @brief The previous node, or NULL at the start or for a NULL node. */
QLIB_NODISCARD QLIB_EXPORT ql_list_node_t* ql_list_prev(const ql_list_node_t* node);

/** @brief Number of elements. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_list_size(const ql_list_t* list);

/** @brief True when the list is empty or NULL. */
QLIB_NODISCARD QLIB_EXPORT bool ql_list_empty(const ql_list_t* list);

/** @brief Element size the list was created with. Zero for NULL. */
QLIB_NODISCARD QLIB_EXPORT size_t ql_list_elem_size(const ql_list_t* list);

/**
 * @brief Remove every element, keeping the list usable.
 *
 * Every node handle from this list becomes invalid. Passing NULL is a no-op.
 */
QLIB_EXPORT void ql_list_clear(ql_list_t* list);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_LIST_H */
