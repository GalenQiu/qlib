#include "qlib/ql_list.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * libFuzzer entry point for ql_list.
 *
 * A doubly linked list fails in one of three ways that a single unit test will
 * not catch: a link updated on only one side, a size counter that drifts from
 * the real node count, and a cycle. All three are visible from outside the
 * library, so instead of maintaining a shadow model this checks the structure
 * after every operation:
 *
 *   - forward and backward traversal both visit exactly ql_list_size() nodes;
 *   - a traversal longer than the number of possible nodes means a cycle;
 *   - every node's successor's predecessor is that node again.
 *
 * The same run also exercises the property the list exists for: a saved node
 * handle stays usable while other nodes come and go around it.
 */

#define SLOTS 16

/**
 * @brief Abort unless the list is structurally sound.
 *
 * @param max_nodes Upper bound on how many nodes the list can legitimately
 *                  hold, used to detect a cycle. It is the number of bytes fed
 *                  in plus one, because every operation may add at most one
 *                  node -- not SLOTS, which is only how many handles are kept.
 */
static void check_structure(ql_list_t* list, size_t max_nodes)
{
    size_t forward = 0;
    size_t backward = 0;
    size_t size = ql_list_size(list);
    ql_list_node_t* node;

    for (node = ql_list_first(list); node != NULL; node = ql_list_next(node))
    {
        forward++;
        if (forward > max_nodes)
        {
            abort(); /* a cycle, or a corrupted link */
        }
    }
    for (node = ql_list_last(list); node != NULL; node = ql_list_prev(node))
    {
        backward++;
        if (backward > max_nodes)
        {
            abort();
        }
    }

    if (forward != size || backward != size)
    {
        abort();
    }
    if (ql_list_empty(list) != (size == 0))
    {
        abort();
    }
    if ((size == 0) != (ql_list_first(list) == NULL) || (size == 0) != (ql_list_last(list) == NULL))
    {
        abort();
    }

    /* Every link has to be mutual. */
    for (node = ql_list_first(list); node != NULL; node = ql_list_next(node))
    {
        ql_list_node_t* next = ql_list_next(node);

        if (next != NULL && ql_list_prev(next) != node)
        {
            abort();
        }
    }
}

/** @brief Whether @p candidate is currently reachable in @p list. */
static bool reachable(ql_list_t* list, ql_list_node_t* candidate)
{
    ql_list_node_t* node;

    for (node = ql_list_first(list); node != NULL; node = ql_list_next(node))
    {
        if (node == candidate)
        {
            return true;
        }
    }
    return false;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    ql_list_t* list = NULL;
    ql_list_node_t* nodes[SLOTS];
    size_t i;
    size_t slot;

    for (slot = 0; slot < SLOTS; slot++)
    {
        nodes[slot] = NULL;
    }

    if (ql_list_create(NULL, sizeof(int), &list) != QL_OK)
    {
        abort();
    }

    for (i = 0; i < size; i++)
    {
        unsigned op = data[i] % 5U;
        size_t target = (i + data[i]) % SLOTS;
        int value = (int)i;

        switch (op)
        {
        case 0:
            if (ql_list_push_back(list, &value, &nodes[target]) != QL_OK)
            {
                abort();
            }
            break;

        case 1:
            if (ql_list_push_front(list, &value, &nodes[target]) != QL_OK)
            {
                abort();
            }
            break;

        case 2:
            (void)ql_list_pop_front(list, NULL);
            /* A popped node may have been one we were holding. */
            for (slot = 0; slot < SLOTS; slot++)
            {
                if (nodes[slot] != NULL && !reachable(list, nodes[slot]))
                {
                    nodes[slot] = NULL;
                }
            }
            break;

        case 3:
            (void)ql_list_pop_back(list, NULL);
            for (slot = 0; slot < SLOTS; slot++)
            {
                if (nodes[slot] != NULL && !reachable(list, nodes[slot]))
                {
                    nodes[slot] = NULL;
                }
            }
            break;

        default:
            /* Remove a node we still hold, if it is still in the list. */
            if (nodes[target] != NULL && reachable(list, nodes[target]))
            {
                if (ql_list_remove(list, nodes[target]) != QL_OK)
                {
                    abort();
                }
                nodes[target] = NULL;
            }
            break;
        }

        check_structure(list, size + 1);

        /* Every handle that is still in the list must still read back
         * something, which is the guarantee the node handle exists for. */
        for (slot = 0; slot < SLOTS; slot++)
        {
            if (nodes[slot] != NULL)
            {
                int out = 0;

                if (!reachable(list, nodes[slot]))
                {
                    continue;
                }
                if (ql_list_node_value(list, nodes[slot], &out) != QL_OK)
                {
                    abort();
                }
            }
        }
    }

    ql_list_clear(list);
    if (!ql_list_empty(list) || ql_list_size(list) != 0)
    {
        abort();
    }
    check_structure(list, size + 1);

    ql_list_destroy(list);
    return 0;
}
