#include "qlib/ql_list.h"

#include "ql_internal.h"

#include <string.h>

/*
 * A node header followed by the element bytes.
 *
 * The header size is rounded up to a multiple of the strictest alignment, so
 * that the element would land on a properly aligned address if a future API
 * ever hands out a pointer to it. Today nothing does: elements only ever move
 * through memcpy, which is why the current API does not depend on the element
 * type's alignment at all.
 */
struct ql_list_node
{
    struct ql_list_node* prev;
    struct ql_list_node* next;
};

#define QL_LIST_ALIGN _Alignof(max_align_t)
#define QL_LIST_HEADER_SIZE ((sizeof(ql_list_node_t) + QL_LIST_ALIGN - 1) / QL_LIST_ALIGN * QL_LIST_ALIGN)

struct ql_list
{
    ql_allocator_t alloc;
    size_t elem_size;
    size_t size;
    ql_list_node_t* head;
    ql_list_node_t* tail;
};

/** @brief The element bytes stored in a node. */
static void* ql_list_elem_of(ql_list_node_t* node)
{
    return (unsigned char*)node + QL_LIST_HEADER_SIZE;
}

static const void* ql_list_elem_of_const(const ql_list_node_t* node)
{
    return (const unsigned char*)node + QL_LIST_HEADER_SIZE;
}

/** @brief Allocate a detached node holding a copy of @p elem. */
static ql_list_node_t* ql_list_node_new(ql_list_t* list, const void* elem)
{
    size_t bytes;
    ql_list_node_t* node;

    if (ql_size_add_overflow(QL_LIST_HEADER_SIZE, list->elem_size, &bytes))
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_list: node size overflow");
        return NULL;
    }

    node = ql_alloc(&list->alloc, bytes);
    if (node == NULL)
    {
        return NULL;
    }

    node->prev = NULL;
    node->next = NULL;
    memcpy(ql_list_elem_of(node), elem, list->elem_size);
    return node;
}

/** @brief Link @p inserted immediately before @p successor. */
static void ql_list_link_before(ql_list_t* list, ql_list_node_t* successor, ql_list_node_t* inserted)
{
    inserted->next = successor;
    inserted->prev = successor->prev;

    if (successor->prev != NULL)
    {
        successor->prev->next = inserted;
    }
    else
    {
        list->head = inserted;
    }
    successor->prev = inserted;
    list->size++;
}

ql_ret_t ql_list_create(const ql_allocator_t* alloc, size_t elem_size, ql_list_t** out)
{
    ql_list_t* list;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_create: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    if (elem_size == 0)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_create: elem_size is 0");
        return QL_ERR_INVALID_ARG;
    }

    list = ql_alloc(alloc, sizeof *list);
    if (list == NULL)
    {
        return QL_ERR_NOMEM;
    }

    list->alloc = alloc != NULL ? *alloc : ql_default_allocator();
    list->elem_size = elem_size;
    list->size = 0;
    list->head = NULL;
    list->tail = NULL;

    *out = list;
    return QL_OK;
}

void ql_list_clear(ql_list_t* list)
{
    ql_list_node_t* node;

    if (list == NULL)
    {
        return;
    }

    node = list->head;
    while (node != NULL)
    {
        ql_list_node_t* next = node->next;

        ql_dealloc(&list->alloc, node);
        node = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void ql_list_destroy(ql_list_t* list)
{
    ql_allocator_t alloc;

    if (list == NULL)
    {
        return;
    }

    /* Copy the allocator out before the control block itself goes away. */
    alloc = list->alloc;
    ql_list_clear(list);
    ql_dealloc(&alloc, list);
}

ql_ret_t ql_list_push_front(ql_list_t* list, const void* elem, ql_list_node_t** out_node)
{
    ql_list_node_t* node;

    if (list == NULL || elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_push_front: list or elem is NULL");
        return QL_ERR_INVALID_ARG;
    }

    node = ql_list_node_new(list, elem);
    if (node == NULL)
    {
        return QL_ERR_NOMEM;
    }

    if (list->head != NULL)
    {
        ql_list_link_before(list, list->head, node);
    }
    else
    {
        list->head = node;
        list->tail = node;
        list->size = 1;
    }

    if (out_node != NULL)
    {
        *out_node = node;
    }
    return QL_OK;
}

ql_ret_t ql_list_push_back(ql_list_t* list, const void* elem, ql_list_node_t** out_node)
{
    ql_list_node_t* node;

    if (list == NULL || elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_push_back: list or elem is NULL");
        return QL_ERR_INVALID_ARG;
    }

    node = ql_list_node_new(list, elem);
    if (node == NULL)
    {
        return QL_ERR_NOMEM;
    }

    node->prev = list->tail;
    if (list->tail != NULL)
    {
        list->tail->next = node;
    }
    else
    {
        list->head = node;
    }
    list->tail = node;
    list->size++;

    if (out_node != NULL)
    {
        *out_node = node;
    }
    return QL_OK;
}

ql_ret_t ql_list_insert_before(ql_list_t* list, ql_list_node_t* node, const void* elem, ql_list_node_t** out_node)
{
    ql_list_node_t* fresh;

    if (list == NULL || node == NULL || elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_insert_before: list, node or elem is NULL");
        return QL_ERR_INVALID_ARG;
    }

    fresh = ql_list_node_new(list, elem);
    if (fresh == NULL)
    {
        return QL_ERR_NOMEM;
    }

    ql_list_link_before(list, node, fresh);
    if (out_node != NULL)
    {
        *out_node = fresh;
    }
    return QL_OK;
}

ql_ret_t ql_list_insert_after(ql_list_t* list, ql_list_node_t* node, const void* elem, ql_list_node_t** out_node)
{
    ql_list_node_t* fresh;

    if (list == NULL || node == NULL || elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_insert_after: list, node or elem is NULL");
        return QL_ERR_INVALID_ARG;
    }

    fresh = ql_list_node_new(list, elem);
    if (fresh == NULL)
    {
        return QL_ERR_NOMEM;
    }

    fresh->prev = node;
    fresh->next = node->next;

    if (node->next != NULL)
    {
        node->next->prev = fresh;
    }
    else
    {
        list->tail = fresh;
    }
    node->next = fresh;
    list->size++;

    if (out_node != NULL)
    {
        *out_node = fresh;
    }
    return QL_OK;
}

ql_ret_t ql_list_remove(ql_list_t* list, ql_list_node_t* node)
{
    if (list == NULL || node == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_remove: list or node is NULL");
        return QL_ERR_INVALID_ARG;
    }

    if (node->prev != NULL)
    {
        node->prev->next = node->next;
    }
    else
    {
        list->head = node->next;
    }

    if (node->next != NULL)
    {
        node->next->prev = node->prev;
    }
    else
    {
        list->tail = node->prev;
    }

    list->size--;
    ql_dealloc(&list->alloc, node);
    return QL_OK;
}

ql_ret_t ql_list_pop_front(ql_list_t* list, void* out_elem)
{
    if (list == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_pop_front: list is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (list->head == NULL)
    {
        return QL_ERR_NOT_FOUND;
    }

    if (out_elem != NULL)
    {
        memcpy(out_elem, ql_list_elem_of_const(list->head), list->elem_size);
    }
    return ql_list_remove(list, list->head);
}

ql_ret_t ql_list_pop_back(ql_list_t* list, void* out_elem)
{
    if (list == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_pop_back: list is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (list->tail == NULL)
    {
        return QL_ERR_NOT_FOUND;
    }

    if (out_elem != NULL)
    {
        memcpy(out_elem, ql_list_elem_of_const(list->tail), list->elem_size);
    }
    return ql_list_remove(list, list->tail);
}

ql_ret_t ql_list_front(const ql_list_t* list, void* out_elem)
{
    if (list == NULL || out_elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_front: list or out_elem is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (list->head == NULL)
    {
        return QL_ERR_NOT_FOUND;
    }

    memcpy(out_elem, ql_list_elem_of_const(list->head), list->elem_size);
    return QL_OK;
}

ql_ret_t ql_list_back(const ql_list_t* list, void* out_elem)
{
    if (list == NULL || out_elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_back: list or out_elem is NULL");
        return QL_ERR_INVALID_ARG;
    }
    if (list->tail == NULL)
    {
        return QL_ERR_NOT_FOUND;
    }

    memcpy(out_elem, ql_list_elem_of_const(list->tail), list->elem_size);
    return QL_OK;
}

ql_ret_t ql_list_node_value(const ql_list_t* list, const ql_list_node_t* node, void* out_elem)
{
    if (list == NULL || node == NULL || out_elem == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_list_node_value: list, node or out_elem is NULL");
        return QL_ERR_INVALID_ARG;
    }

    memcpy(out_elem, ql_list_elem_of_const(node), list->elem_size);
    return QL_OK;
}

ql_list_node_t* ql_list_first(const ql_list_t* list)
{
    return list != NULL ? list->head : NULL;
}

ql_list_node_t* ql_list_last(const ql_list_t* list)
{
    return list != NULL ? list->tail : NULL;
}

ql_list_node_t* ql_list_next(const ql_list_node_t* node)
{
    return node != NULL ? node->next : NULL;
}

ql_list_node_t* ql_list_prev(const ql_list_node_t* node)
{
    return node != NULL ? node->prev : NULL;
}

size_t ql_list_size(const ql_list_t* list)
{
    return list != NULL ? list->size : 0;
}

bool ql_list_empty(const ql_list_t* list)
{
    return list == NULL || list->size == 0;
}

size_t ql_list_elem_size(const ql_list_t* list)
{
    return list != NULL ? list->elem_size : 0;
}
