# 向量（动态数组）

`ql_vector_t` 是一个可增长的、连续存储的数组，元素大小固定。它是一个不透明类型：
你始终只持有一个指针，其内存布局是库私有的。

```c
#include <qlib/qlib.h>

ql_vector_t* vec = NULL;
if (ql_vector_create(NULL, sizeof(int), &vec) != QL_OK) {
    return -1;
}
/* ... */
ql_vector_destroy(vec);
```

## 创建与销毁

```c
ql_ret_t ql_vector_create(const ql_allocator_t* alloc, size_t elem_size, ql_vector_t** out);
void     ql_vector_destroy(ql_vector_t* vec);
```

`elem_size` 是一个元素的字节大小，必须非零。分配器会被复制进向量，因此你传入的
结构体不必在这次调用之后继续存活；`NULL` 表示选择默认分配器。

失败时 `ql_vector_create()` 返回一个错误码并把 `*out` 设为 `NULL`，因此创建失败
绝不会留下悬空指针。`ql_vector_destroy(NULL)` 是空操作，这让清理路径更简单。

## 添加与删除

| 函数 | 行为 |
|---|---|
| `ql_vector_push_back(vec, elem)` | 追加一份副本。满时扩容 |
| `ql_vector_pop_back(vec, out)` | 移除最后一个元素并写入 `out`；`out` 可以为 `NULL` 表示丢弃。为空时返回 `QL_ERR_EMPTY` |
| `ql_vector_insert(vec, index, elem)` | 插入后副本位于 `index`；`index == size` 表示追加 |
| `ql_vector_remove(vec, index)` | 移除 `index` 处的元素，其余元素前移 |
| `ql_vector_assign(vec, index, elem)` | 覆盖 `index` 处的元素 |
| `ql_vector_clear(vec)` | 丢弃所有元素，保留容量 |

```c
int i;
for (i = 0; i < 100; i++) {
    if (ql_vector_push_back(vec, &i) != QL_OK) {
        return -1;
    }
}

int last = 0;
if (ql_vector_pop_back(vec, &last) == QL_OK) {
    /* last == 99 */
}
```

`insert` 接受 `index == size`（追加），但对更大的值会以 `QL_ERR_OUT_OF_RANGE`
拒绝。`remove` 和 `assign` 要求 `index < size`。

## 读取

```c
void*  ql_vector_at(ql_vector_t* vec, size_t index);   /* 越界时返回 NULL */
void*  ql_vector_data(ql_vector_t* vec);               /* 为空时返回 NULL */
size_t ql_vector_size(const ql_vector_t* vec);
size_t ql_vector_capacity(const ql_vector_t* vec);
size_t ql_vector_elem_size(const ql_vector_t* vec);
bool   ql_vector_empty(const ql_vector_t* vec);
```

这些访问函数返回 `void*`，因此在解引用前要先做类型转换：

```c
int* slot = ql_vector_at(vec, 3);
if (slot != NULL) {
    printf("%d\n", *slot);
} else {
    /* 索引 3 越界，或者 vec 是 NULL */
}
```

这些查询函数在 `NULL` 向量上是安全的：它们会报告 0、`NULL` 或空。这是有意为之，
这样类似析构函数和日志的代码就不必在每次调用前检查 NULL。

## 容量与扩容

| 函数 | 行为 |
|---|---|
| `ql_vector_reserve(vec, capacity)` | 确保至少能容纳 `capacity` 个元素。从不缩小 |
| `ql_vector_resize(vec, count)` | 设置元素个数 |

```c
ql_vector_reserve(vec, 1000);   /* 只分配一次，而不是很多次 */
for (i = 0; i < 1000; i++) {
    ql_vector_push_back(vec, &i);   /* 不会发生重新分配 */
}
```

容量从 0 开始按几何级数增长，从初始的 8 开始翻倍，因此 `push_back` 的摊还复杂度
是 O(1)。当中间重新分配的开销不可忽略时，应在已知循环次数的情况下提前预留容量。

`ql_vector_resize()` 扩容时会把新暴露出来的元素清零。缩容只是降低元素个数；这些
字节仍留在缓冲区中，直到被覆盖，因此不要把缩容当作安全擦除。

这两个函数都会以 `QL_ERR_OVERFLOW` 拒绝无法用 `count * elem_size` 表示的大小；
任何失败发生时，向量都保持原样。

## 元素类型

元素通过 `memcpy` 按字节复制，因此元素类型必须是可平凡复制的：不能有需要深拷贝的
内部指针，也不能有构造函数。

```c
typedef struct {
    int    id;
    double x, y;
} point;

ql_vector_t* points = NULL;
ql_vector_create(NULL, sizeof(point), &points);

point p = {1, 2.0, 3.0};
ql_vector_push_back(points, &p);
```

对于 `char*` 的向量，向量存储的是指针而不是字符串；它们指向的内容归你所有。

## 指针失效

来自 `ql_vector_at()` 和 `ql_vector_data()` 的指针指向向量的内部缓冲区。它们会在
以下情况失效：

- 任何增加容量的操作：`push_back`、`insert`、`reserve`、向上 `resize`
- 在该元素处或其之前进行的 `insert` 和 `remove`，它们会移动数据

它们能经受 `pop_back`、`assign`、`clear` 以及对其他元素的写入而不失效。

```c
int* a = ql_vector_at(vec, 0);
ql_vector_push_back(vec, &value);   /* a 现在悬空了 */
a = ql_vector_at(vec, 0);           /* 再次使用前需要重新获取 */
```

这是最容易踩坑的一条规则，它与 C++ 中 `std::vector` 的规则相同。

## 已处理别名情况

传入一个指向向量自身缓冲区的元素是受支持的，并且行为正确：

```c
/* 把第一个元素复制一份追加到末尾。 */
if (ql_vector_push_back(vec, ql_vector_at(vec, 0)) != QL_OK) { /* 处理 */ }

/* 把元素 3 复制到最前面，其余元素后移。 */
if (ql_vector_insert(vec, 0, ql_vector_at(vec, 3)) != QL_OK) { /* 处理 */ }

/* 自赋值，必须使用 memmove 语义，而不是 memcpy。 */
if (ql_vector_assign(vec, 0, ql_vector_at(vec, 0)) != QL_OK) { /* 处理 */ }
```

那些 `if (...)` 包裹不是装饰：每个会修改向量的函数都标记了 `QLIB_NODISCARD`，
所以忽略返回值会产生编译器警告。本文档中看起来忽略返回值的片段是供阅读的片段，
不要原样复制。

内部实现会先把元素复制到一个临时变量，然后再改动缓冲区。否则，扩容时重新分配的
正是正在被读取的那些字节。你不需要为此做额外处理，也不需要自己把元素复制出来。

## 各函数对应的错误码

| 情况 | 错误码 | 由谁抛出 |
|---|---|---|
| `vec` 或 `elem` 为 `NULL` | `QL_ERR_INVALID_ARG` | 所有接受它们的函数 |
| `elem_size == 0` | `QL_ERR_INVALID_ARG` | `ql_vector_create` |
| `out == NULL` | `QL_ERR_INVALID_ARG` | `ql_vector_create` |
| `index > size` | `QL_ERR_OUT_OF_RANGE` | `ql_vector_insert` |
| `index >= size` | `QL_ERR_OUT_OF_RANGE` | `ql_vector_remove`, `ql_vector_assign` |
| `size == 0` | `QL_ERR_EMPTY` | `ql_vector_pop_back` |
| `capacity * elem_size` 溢出 | `QL_ERR_OVERFLOW` | `reserve`, `resize`, `push_back`, `insert` |
| 分配失败 | `QL_ERR_NOMEM` | `reserve`, `resize`, `push_back`, `insert` |

上述每一种情况都会同时上报给已安装的[错误回调](errors.md)，消息中会指明函数名。

## 完整示例

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_vector_t* vec = NULL;
    size_t i;

    if (ql_vector_create(NULL, sizeof(int), &vec) != QL_OK) {
        return 1;
    }

    /* 先 reserve 一次，下面的循环就不会再重新分配。 */
    if (ql_vector_reserve(vec, 8) != QL_OK) {
        ql_vector_destroy(vec);
        return 1;
    }

    for (i = 0; i < 8; i++) {
        int value = (int)i * 10;
        if (ql_vector_push_back(vec, &value) != QL_OK) {
            ql_vector_destroy(vec);
            return 1;
        }
    }
    /* vec == {0, 10, 20, 30, 40, 50, 60, 70}，size == capacity == 8 */

    /* 复制最后一个元素；这会强制从 8 扩容到 16。 */
    if (ql_vector_push_back(vec, ql_vector_at(vec, 7)) != QL_OK) {
        ql_vector_destroy(vec);
        return 1;
    }

    /* 丢弃前两个元素。两者都可能失败，因此都要检查：每个会修改
     * ql_vector 的函数都标记了 QLIB_NODISCARD。 */
    if (ql_vector_remove(vec, 0) != QL_OK || ql_vector_remove(vec, 0) != QL_OK) {
        ql_vector_destroy(vec);
        return 1;
    }

    printf("size %zu, capacity %zu\n", ql_vector_size(vec), ql_vector_capacity(vec));
    for (i = 0; i < ql_vector_size(vec); i++) {
        printf("%d ", *(int*)ql_vector_at(vec, i));
    }
    printf("\n");

    ql_vector_destroy(vec);
    return 0;
}
```

输出：

```
size 7, capacity 16
20 30 40 50 60 70 70
```
