# 错误处理

qlib 中每个可能失败的函数都返回 `ql_ret_t`。`QL_OK` 的值为 0，因此惯用的检查
写法是 `if (rc != QL_OK)`。本库从不把 `errno` 当作自己的错误通道。

## 返回码

| 错误码 | `ql_strerror()` 文本 | 含义 |
|---|---|---|
| `QL_OK` | `success` | 没有错误 |
| `QL_ERR_NOMEM` | `out of memory` | 分配失败 |
| `QL_ERR_INVALID_ARG` | `invalid argument` | NULL 指针，或参数超出其约定范围 |
| `QL_ERR_OUT_OF_RANGE` | `value out of range` | 可以表示，但超出有效范围 |
| `QL_ERR_EMPTY` | `container is empty` | 该操作需要非空容器 |
| `QL_ERR_NOT_FOUND` | `not found` | 查找没有找到任何结果 |
| `QL_ERR_IO` | `I/O error` | I/O 操作失败 |
| `QL_ERR_SYS` | `system error` | 未映射的平台或操作系统错误 |
| `QL_ERR_AGAIN` | `temporarily unavailable` | 重试可能成功 |
| `QL_ERR_TIMEOUT` | `timed out` | 超过了截止时间 |
| `QL_ERR_BUSY` | `resource busy` | 资源正在被使用 |
| `QL_ERR_EXISTS` | `already exists` | 该项已经存在 |
| `QL_ERR_NOT_SUPPORTED` | `not supported` | 在此平台或此构建中不可用 |
| `QL_ERR_OVERFLOW` | `arithmetic overflow` | 算术结果无法容纳；该请求已被拒绝 |

新增的错误码只会追加，因此错误码的数值在不同版本之间保持稳定。请把这个集合视为
开放的：要写 `default:` 分支，而不要假定自己已经枚举了全部情况。参见
[ABI 与版本](../technical/abi-and-versioning.md)。

## 把错误码转换为文本

```c
#include <qlib/qlib.h>
#include <stdio.h>

ql_ret_t rc = ql_vector_reserve(vec, 1000);
if (rc != QL_OK) {
    fprintf(stderr, "reserve failed: %s\n", ql_strerror(rc));
    return rc;
}
```

`ql_strerror()` 是**全函数**：它从不返回 `NULL`，对于不是已知错误码的值会返回
`"unknown error"`。因此，对来历不受你控制的 `ql_ret_t`（例如被破坏的结构体或
ABI 不匹配得到的值）调用它是安全的。它返回指向静态字符串的指针，所以不要释放它，
也不要指望它被本地化。

## 映射 `errno`

当你拿到一个平台错误并想得到 qlib 错误码时：

```c
#include <errno.h>

FILE* f = fopen(path, "rb");
if (f == NULL) {
    ql_ret_t rc = ql_ret_from_errno(errno);
    fprintf(stderr, "%s\n", ql_strerror(rc));   /* 例如 "not found" */
    return rc;
}
```

`0` 映射为 `QL_OK`，任何无法识别的值都映射为 `QL_ERR_SYS`，因此这个函数在两个
方向上都是全函数。在平台定义了相应宏的前提下，映射覆盖 `ENOMEM`、`EINVAL`、
`ERANGE`、`ENOENT`、`EIO`、`EEXIST`、`EAGAIN`、`ETIMEDOUT`、`EBUSY`、
`EOVERFLOW` 和 `ENOTSUP`；MSVC 的 `errno` 集合比 POSIX 的窄，因此其中若干项在
Windows 上并不存在。

## 错误回调

`ql_ret_t` 告诉直接调用者发生了什么。而错误回调适用于另一种情况：无论失败源自
多深，你都希望有一个统一的地方记录或统计它们：

```c
#include <qlib/qlib.h>
#include <stdio.h>

static void on_error(ql_ret_t code, const char* message, void* userdata)
{
    int* count = userdata;
    (*count)++;
    fprintf(stderr, "qlib: %s (%s)\n", message, ql_strerror(code));
}

int failures = 0;
ql_set_error_handler(on_error, &failures);

/* ... 使用本库；此时失败会在发生时到达 on_error ... */

ql_set_error_handler(NULL, NULL);   /* 再次移除它 */
```

值得了解的语义：

- 回调是在**失败发生的位置**、由遇到失败的那个线程调用的。
- `message` 是一个静态字符串，比 `ql_strerror()` 提供更多上下文，例如
  `"ql_alloc: allocation failed"`。它永远不会是 `NULL`。
- `userdata` 就是传给 `ql_set_error_handler()` 的那个值。
- **回调不得回调进本库。** 它在操作回退期间运行，重入可能造成死锁或状态损坏。
  请让它只做日志、计数或置标志位。
- 失败仍会把错误码（或 `NULL`）返回给调用者。回调是通知通道，不能替代对返回值
  的检查。
- 设置回调应当在启动阶段完成，早于其他线程读取它。

未安装回调时，除返回值之外失败是静默的。这是默认行为，也正是本库能用于在本地
处理错误的代码的原因。

## 检查什么，不检查什么

qlib 会检查调用者从库外部确实可能弄错的参数：NULL 指针、元素大小为零、越界索引，
以及会溢出的算术运算。这些情况会产生 `QL_ERR_INVALID_ARG`、
`QL_ERR_OUT_OF_RANGE` 或 `QL_ERR_OVERFLOW`，而不是未定义行为。

它不会在运行期重复检查内部不变量。这些由调试构建中的 `assert()` 和测试套件
覆盖，具体见项目的[贡献规则](../../../CONTRIBUTING.md)。

## 模式

直接返回错误码，而不要对它做转换，这样调用者能保留完整信息：

```c
static ql_ret_t load_all(ql_vector_t* vec, const char* path)
{
    ql_ret_t rc = read_into(vec, path);
    if (rc != QL_OK) {
        return rc;              /* 不要用 QL_ERR_IO：那会丢失原因 */
    }
    return ql_vector_reserve(vec, 128);
}
```

当一个函数可能因多种原因失败、而调用者并不关心具体是哪种时，`ql_ret_t` 仍然优于
裸的 `int`，因为调用者可以直接把它传给 `ql_strerror()` 和错误回调，无需转换表。
