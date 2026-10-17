# 字符串

`<qlib/ql_str.h>` 提供 `ql_str_t`：一个始终以 NUL 结尾的可增长字节缓冲区。它是
`ql_vector` 的字符串版本——同样的分配器规则，同样"检查返回值"的纪律——另加两条
让构建文本更顺手的保证：

- 即使字符串为空，`ql_str_cstr()` 也总是返回有效的 C 字符串，因此结果可以直接传给
  `printf`、`fopen` 或任何 C API。
- 每个修改操作要么成功，要么让原有内容保持不变。不存在长度与缓冲区不一致的状态。

## 构建字符串

```c
ql_str_t* text = NULL;

if (ql_str_create(NULL, &text) != QL_OK) {
    /* 内存不足 */
}
if (ql_str_append(text, "Hello") != QL_OK ||
    ql_str_append_char(text, ' ') != QL_OK ||
    ql_str_append_printf(text, "%s", "world") != QL_OK) {
    ql_str_destroy(text);
    /* 处理失败 */
}
```

可用的修改函数：

| 函数 | 作用 |
|---|---|
| `ql_str_assign` / `ql_str_assign_n` | 替换内容 |
| `ql_str_append` | 追加一个 C 字符串 |
| `ql_str_append_n` | 追加一段字节范围，包含 NUL 字节 |
| `ql_str_append_char` | 追加一个字节 |
| `ql_str_append_str` | 追加另一个 `ql_str_t` |
| `ql_str_append_printf` / `ql_str_append_vprintf` | 追加格式化文本 |
| `ql_str_clear` | 使其为空，保留缓冲区 |
| `ql_str_reserve` | 提前扩大缓冲区；从不缩小 |

`ql_str_append_printf` 会扩容并重试，而不是截断，因此即使格式化结果很大也能处理。
它把格式应用在 `va_list` 的一份副本上，因此失败的一次尝试不会消耗掉调用方的
那份副本。

## 读回内容

```c
printf("%s\n", ql_str_cstr(text));   /* "Hello world" */
printf("%zu bytes\n", ql_str_size(text));
```

对于有效的字符串，`ql_str_cstr()` 从不返回 `NULL`；空字符串返回 `""`。
`ql_str_size()` 是权威长度，且不会调用 `strlen`。

## 内嵌 NUL 字节

`ql_str_assign_n()` 和 `ql_str_append_n()` 接受包含 NUL 字节的数据。在此之后，
长度会大于 `strlen()` 报告的值：

```c
static const char data[] = {'a', 'b', '\0', 'c', 'd'};

ql_str_assign_n(text, data, sizeof data);
printf("%zu\n", ql_str_size(text));          /* 5 */
printf("%zu\n", strlen(ql_str_cstr(text)));  /* 2  */
```

这本身不会出问题，但任何把结果当作 C 字符串处理的代码都会在第一个 NUL 处停下。
`ql_str_equal_cstr()` 先比较长度，因此含有内嵌 NUL 的字符串永远不会等于可见前缀
相同的 C 字符串。

如果你从不追加 NUL 字节，这些都不影响你。

## 比较与查找

```c
if (ql_str_compare(a, b) == 0) { /* 相等 */ }
if (ql_str_equal_cstr(a, "expected")) { /* ... */ }

size_t index;
if (ql_str_find(a, "needle", &index) == QL_OK) {
    printf("found at %zu\n", index);
}
```

`ql_str_compare()` 在较短的长度内逐字节比较，并把较短的字符串视为较小者——对于
不含内嵌 NUL 的字符串，这与 `strcmp` 的顺序相同；在任何情况下它都给出全序。

当目标不存在时，`ql_str_find()` 报告 `QL_ERR_NOT_FOUND` 而不是错误，并且在这种
情况下不改动输出。空的目标在索引 0 处匹配，与 `strstr` 一样。

## 把字符串追加到自身

```c
ql_str_append_str(text, text);       /* 复制内容 */
ql_str_append(text, ql_str_cstr(text));  /* 效果相同 */
```

两种写法都受支持。扩容期间缓冲区可能移动，因此之后会通过偏移量重新推导源位置。
这是实现中唯一需要判断某个指针是否属于自身缓冲区的地方。

## 完整示例

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_str_t* greeting = NULL;

    if (ql_str_create_from(NULL, "Hello", &greeting) != QL_OK) {
        return 1;
    }
    if (ql_str_append(greeting, ", ") != QL_OK ||
        ql_str_append_printf(greeting, "%s!", "world") != QL_OK) {
        ql_str_destroy(greeting);
        return 1;
    }

    printf("%s (%zu bytes)\n", ql_str_cstr(greeting), ql_str_size(greeting));

    ql_str_destroy(greeting);
    return 0;
}
```

```
Hello, world! (13 bytes)
```

## 性能

由 `bin/qlib_bench` 在开发机上测得：

| 操作 | 开销 |
|---|---|
| `ql_str_append_char`（摊还） | 11 ns |
| `ql_str_append_printf` | 31 ns |

追加一个字符是摊还 O(1)：缓冲区会翻倍。`printf` 的开销主要来自 `vsnprintf`，
而不是字符串本身。

## 错误码

| 情况 | 错误码 |
|---|---|
| `NULL` 字符串、源、格式或输出 | `QL_ERR_INVALID_ARG` |
| 分配失败 | `QL_ERR_NOMEM` |
| 长度或容量会使 `size_t` 溢出 | `QL_ERR_OVERFLOW` |
| `ql_str_find()` 未找到目标 | `QL_ERR_NOT_FOUND` |

除 `ql_str_find()` 返回的 `QL_ERR_NOT_FOUND` 之外，上述每一种情况也都会上报给
已安装的[错误回调](errors.md)；`QL_ERR_NOT_FOUND` 是一个普通的答案，而不是故障。
