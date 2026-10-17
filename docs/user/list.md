# Linked list

`<qlib/ql_list.h>` provides `ql_list_t`: a doubly linked list of fixed size
elements.

`ql_vector` covers most needs and beats a list at everything except two:
inserting or removing in the middle in constant time, and holding a reference to
an element that survives other elements coming and going. Both need a handle, and
that is what `ql_list_node_t` is.

## Node handles

A node stays valid until it is removed — or until the list is cleared or
destroyed — no matter how many other nodes are added or removed around it.
Removing it costs nothing beyond unlinking it.

```c
ql_list_t* list = NULL;
ql_list_node_t* node = NULL;
int value = 2;

ql_list_create(NULL, sizeof(int), &list);
ql_list_push_back(list, &value, &node);   /* node now refers to this element */

/* Any amount of activity elsewhere... */
value = 99;
ql_list_push_front(list, &value, NULL);
ql_list_push_back(list, &value, NULL);

/* ...and the handle still reads back 2. */
int out = 0;
ql_list_node_value(list, node, &out);     /* out == 2 */

ql_list_remove(list, node);               /* O(1), no search */
```

`ql_list_node_value()` takes the list as well as the node, because the list is
what knows the element size.

## Building and draining

| Function | Effect |
|---|---|
| `ql_list_push_front` / `ql_list_push_back` | Insert at an end; optionally return the node |
| `ql_list_insert_before` / `ql_list_insert_after` | Insert next to an existing node |
| `ql_list_pop_front` / `ql_list_pop_back` | Remove an end and copy the element out |
| `ql_list_front` / `ql_list_back` | Copy an end out without removing |
| `ql_list_remove` | Remove a specific node |
| `ql_list_clear` | Remove everything, keeping the list usable |

`ql_list_pop_front()` and `ql_list_pop_back()` accept a `NULL` output to discard
the element, and report `QL_ERR_NOT_FOUND` on an empty list rather than failing.

## Walking the list

There is no iterator type: nodes are the iteration state.

```c
ql_list_node_t* it;

for (it = ql_list_first(list); it != NULL; it = ql_list_next(it)) {
    int value;
    ql_list_node_value(list, it, &value);
    printf("%d\n", value);
}

/* Backwards uses ql_list_last() and ql_list_prev(). */
```

Removing the current node inside the loop is safe if you advance first:

```c
for (it = ql_list_first(list); it != NULL;) {
    ql_list_node_t* next = ql_list_next(it);

    if (should_drop(it)) {
        ql_list_remove(list, it);   /* `it` is dead; `next` is not */
    }
    it = next;
}
```

## When not to use a list

A list allocates one node per element, so it is much heavier than a vector for
plain appending and much worse for the cache:

| Operation | `ql_vector` | `ql_list` |
|---|---|---|
| Push at the end | 15 ns, amortised, contiguous | 29 ns, one allocation |
| Remove the first element | 39 ns, shifts everything | 15 ns, unlinks |
| Remove an arbitrary element | O(n), shifts | O(1) with a handle |
| Iteration | Contiguous, cache friendly | Pointer chasing |

Reach for `ql_list` when you need stable handles or repeated middle removals, not
as a general purpose container.

## Performance

| Operation | Cost |
|---|---|
| `ql_list_push_back` | 29 ns |
| `ql_list_remove` through a handle | 15 ns |

## A complete example

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_list_t* list = NULL;
    ql_list_node_t* node;
    int i;

    if (ql_list_create(NULL, sizeof(int), &list) != QL_OK) {
        return 1;
    }
    for (i = 0; i < 5; i++) {
        if (ql_list_push_back(list, &i, NULL) != QL_OK) {
            ql_list_destroy(list);
            return 1;
        }
    }

    /* Hold on to the third element (value 2). */
    node = ql_list_first(list);
    node = ql_list_next(node);
    node = ql_list_next(node);

    /* Drop the first two elements; the handle is unaffected. */
    ql_list_pop_front(list, NULL);
    ql_list_pop_front(list, NULL);

    {
        int value = 0;

        ql_list_node_value(list, node, &value);
        printf("still holding %d, size %zu\n", value, ql_list_size(list));
    }

    ql_list_destroy(list);
    return 0;
}
```

```
still holding 2, size 3
```

## Error codes

| Situation | Code |
|---|---|
| `NULL` list, element or node; `elem_size` 0 | `QL_ERR_INVALID_ARG` |
| Allocation failed | `QL_ERR_NOMEM` |
| Node size would overflow `size_t` | `QL_ERR_OVERFLOW` |
| Popping, or peeking at, an empty list | `QL_ERR_NOT_FOUND` |

`QL_ERR_NOT_FOUND` is an ordinary answer rather than a fault, so it is returned
but not reported to the [error handler](errors.md).
