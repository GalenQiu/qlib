# 链表

`<qlib/ql_list.h>` 提供 `ql_list_t`：一个由固定大小元素组成的双向链表。

`ql_vector` 覆盖了大多数需求，除了两点之外在各方面都优于链表：以常数时间在中间
插入或删除，以及持有一个在周围元素不断增删时仍然有效的元素引用。这两点都需要
一个句柄，而 `ql_list_node_t` 就是这样的句柄。

## 节点句柄

只要节点没有被移除——或者链表没有被清空或销毁——它就保持有效，无论周围又添加或
移除了多少其他节点。移除它除了把它从链上摘下之外没有别的开销。

```c
ql_list_t* list = NULL;
ql_list_node_t* node = NULL;
int value = 2;

ql_list_create(NULL, sizeof(int), &list);
ql_list_push_back(list, &value, &node);   /* node 现在指向这个元素 */

/* 别处无论发生多少操作…… */
value = 99;
ql_list_push_front(list, &value, NULL);
ql_list_push_back(list, &value, NULL);

/* ……而句柄仍然读回 2。 */
int out = 0;
ql_list_node_value(list, node, &out);     /* out == 2 */

ql_list_remove(list, node);               /* O(1)，无需查找 */
```

`ql_list_node_value()` 除了节点还需要链表，因为知道元素大小的是链表。

## 构建与取出

| 函数 | 作用 |
|---|---|
| `ql_list_push_front` / `ql_list_push_back` | 在某一端插入；可选地返回节点 |
| `ql_list_insert_before` / `ql_list_insert_after` | 在已有节点旁插入 |
| `ql_list_pop_front` / `ql_list_pop_back` | 移除某一端的元素并把元素复制出来 |
| `ql_list_front` / `ql_list_back` | 复制某一端的元素而不移除 |
| `ql_list_remove` | 移除指定节点 |
| `ql_list_clear` | 移除所有元素，链表仍可使用 |

`ql_list_pop_front()` 和 `ql_list_pop_back()` 接受 `NULL` 输出以丢弃元素，并在
链表为空时报告 `QL_ERR_NOT_FOUND`，而不是失败。

## 遍历链表

没有迭代器类型：节点本身就是迭代状态。

```c
ql_list_node_t* it;

for (it = ql_list_first(list); it != NULL; it = ql_list_next(it)) {
    int value;
    ql_list_node_value(list, it, &value);
    printf("%d\n", value);
}

/* 反向遍历使用 ql_list_last() 和 ql_list_prev()。 */
```

只要先前进到下一个节点，在循环内移除当前节点就是安全的：

```c
for (it = ql_list_first(list); it != NULL;) {
    ql_list_node_t* next = ql_list_next(it);

    if (should_drop(it)) {
        ql_list_remove(list, it);   /* `it` 已失效；`next` 未失效 */
    }
    it = next;
}
```

## 何时不要使用链表

链表为每个元素分配一个节点，因此对于单纯的追加，它比向量重得多，对缓存也差
得多：

| 操作 | `ql_vector` | `ql_list` |
|---|---|---|
| 尾部追加 | 15 ns，摊还，连续 | 29 ns，一次分配 |
| 移除第一个元素 | 39 ns，移动所有元素 | 15 ns，摘链 |
| 移除任意元素 | O(n)，移动 | O(1)，需要句柄 |
| 迭代 | 连续，对缓存友好 | 指针追逐 |

只有在你需要稳定的句柄或反复移除中间元素时才应使用 `ql_list`，不要把它当作通用
容器。

## 性能

| 操作 | 开销 |
|---|---|
| `ql_list_push_back` | 29 ns |
| 通过句柄调用 `ql_list_remove` | 15 ns |

## 完整示例

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

    /* 持有第三个元素（值为 2）。 */
    node = ql_list_first(list);
    node = ql_list_next(node);
    node = ql_list_next(node);

    /* 丢弃前两个元素；句柄不受影响。 */
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

## 错误码

| 情况 | 错误码 |
|---|---|
| `NULL` 链表、元素或节点；`elem_size` 为 0 | `QL_ERR_INVALID_ARG` |
| 分配失败 | `QL_ERR_NOMEM` |
| 节点大小会使 `size_t` 溢出 | `QL_ERR_OVERFLOW` |
| 对空链表执行弹出或窥视 | `QL_ERR_NOT_FOUND` |

`QL_ERR_NOT_FOUND` 是一个普通的答案而不是故障，因此它会被返回，但不会上报给
[错误回调](errors.md)。
