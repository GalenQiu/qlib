# 哈希表

`<qlib/ql_hash.h>` 提供 `ql_hash_t`：一个把固定大小的键映射到固定大小的值的哈希表。
键和值都是原始字节块，其大小在创建时固定，思路与 `ql_vector` 的 `elem_size` 相同。
键和值都按值复制进出，因此无需手工释放任何东西，调用方的内存也永远不会被保留。

查找是摊还 O(1)：在大小为 2 的幂的槽位数组上做开放寻址与线性探测，装载因子保持
在 75% 以下。

## 基本用法

```c
ql_hash_t* squares = NULL;
int i;

if (ql_hash_create(NULL, sizeof(int), sizeof(int), &squares) != QL_OK) {
    return 1;
}
for (i = 1; i <= 5; i++) {
    int result = i * i;
    if (ql_hash_set(squares, &i, &result) != QL_OK) {
        ql_hash_destroy(squares);
        return 1;
    }
}

{
    int key = 4;
    int found = 0;

    if (ql_hash_get(squares, &key, &found) == QL_OK) {
        printf("4 squared is %d\n", found);
    }
}
printf("the table holds %zu entries\n", ql_hash_size(squares));
```

```
4 squared is 16
the table holds 5 entries
```

`ql_hash_set()` 插入或覆盖：设置一个已存在的键会替换其值，并保持条目数不变。

## 键按字节比较

整个键块都用 `memcmp` 比较，因此选择键类型之前有些后果值得了解：

| 键类型 | 可用？ | 说明 |
|---|---|---|
| 整数 | 可以 | 无需担心填充字节 |
| 不含填充字节的结构体 | 可以 | 用初始化器构造它，而不是 `memset` 再加逐字段写入 |
| **含有**填充字节的结构体 | 危险（不可用） | 填充字节也是键的一部分；两个看起来相等的值可能并不匹配 |
| 固定大小的 `char[8]` | 可以 | 未使用的字节必须由调用方清零，否则 "alpha" 和 "alpha\0\0\0garbage" 会不同 |
| `char*` | 不可以 | 它哈希的是指针，而不是文本 |

对于字符串键，先清零缓冲区：

```c
char key[16];

memset(key, 0, sizeof key);
snprintf(key, sizeof key, "%s", word);
ql_hash_set(table, key, &value);
```

键大小为 0 会在创建时被拒绝：那样每个键都会比较相等，这绝不是调用方想要的结果。

## 成员集合

值大小传 0 会把表变成一个集合，它只回答"这个键是否存在？"：

```c
ql_hash_t* seen = NULL;
int id = 7;

ql_hash_create(NULL, sizeof id, 0, &seen);
ql_hash_set(seen, &id, NULL);            /* 没有值需要传入 */
bool there = ql_hash_contains(seen, &id);
ql_hash_get(seen, &id, NULL);            /* QL_OK，不复制任何内容 */
```

## 删除

```c
if (ql_hash_remove(table, &key) == QL_ERR_NOT_FOUND) {
    /* 它原本就不在那里 */
}
```

删除会写入墓碑标记，而不是移动条目，因此位于另一个键探测链更远处的键仍然能被
找到。墓碑标记会被后续插入复用，并在重哈希时清除。

## 遍历

```c
ql_hash_iter_t it;
const void* raw_key;
const void* raw_value;

ql_hash_iter_init(&it, table);
while (ql_hash_iter_next(&it, &raw_key, &raw_value)) {
    int key;
    int value;

    memcpy(&key, raw_key, sizeof key);
    memcpy(&value, raw_value, sizeof value);
    printf("%d -> %d\n", key, value);
}
```

迭代器返回指向表自身存储的指针，在表下一次被修改之前有效。需要什么就复制出来。
**任何修改都会使迭代器失效**：请用 `ql_hash_iter_init()` 重新开始。

不承诺任何顺序，而且删除会改变顺序。

## 性能

| 操作 | 开销 |
|---|---|
| `ql_hash_set`（含扩容） | 61 ns |
| `ql_hash_get`（4096 条目） | 18 ns |

## 错误码

| 情况 | 错误码 |
|---|---|
| `NULL` 表或键；表存储值而值为 `NULL`；`key_size` 为 0 | `QL_ERR_INVALID_ARG` |
| 分配失败 | `QL_ERR_NOMEM` |
| 容量会使 `size_t` 溢出 | `QL_ERR_OVERFLOW` |
| 对不存在的键调用 `ql_hash_get()` / `ql_hash_remove()` | `QL_ERR_NOT_FOUND` |

`QL_ERR_NOT_FOUND` 是一个普通的答案，而不是故障：它会被返回，但不会上报给
[错误回调](errors.md)。`ql_hash_contains()` 则完全不借助错误码回答同一个问题。
