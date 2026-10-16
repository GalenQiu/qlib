# 网络地址

`<qlib/ql_net.h>` 把文本形式的 IPv4 地址转换为值，并能反向转换。它是网络模块
的第一块切片，也是刻意选择的那一块：不需要 socket、不需要平台初始化、也不需要
外部库——它只是对字符串做纯计算，因此在 qlib 支持的每个平台上行为一致，测试时
也不需要网络。

socket、DNS 以及 Windows 所需的 socket 库启动都还不在这里。

> 该模块是可选模块。以 `QLIB_BUILD_NET=OFF` 配置的构建会把它整体省略，此时
> `<qlib/ql_net.h>` 也不会被安装。两种配置下包含 `<qlib/qlib.h>` 都能正常工作。
> 参见[集成](integration.md)。

## 地址类型

```c
typedef enum {
    QL_NET_AF_UNSPEC = 0,   /* 无地址；零值 */
    QL_NET_AF_INET,         /* IPv4 */
    QL_NET_AF_INET6         /* IPv6：已预留，尚未实现 */
} ql_net_family_t;

typedef struct {
    ql_net_family_t family;
    unsigned char   bytes[16];   /* IPv4 使用 bytes[0..3] */
    void*           reserved[2];
} ql_net_addr_t;
```

qlib 使用自己的地址族取值，而不是平台的 `AF_INET` 之类，后者在 Windows 与
POSIX 之间并不相同；把它们暴露出去会把这种差异泄漏给每一个调用方。

该类型很小，除地址族标记之外没有别的不变量，而且调用方需要在栈上构造它，因此
它属于项目允许暴露的值类型之一。它存储十六个字节，尽管 IPv4 只需要四个，这正
是让日后加入 IPv6 成为非破坏性变更的原因。

清零的 `ql_net_addr_t` 是合法值：地址族为 `QL_NET_AF_UNSPEC`，表示"无地址"。

## 解析

```c
#include <qlib/qlib.h>

ql_net_addr_t addr;
if (ql_net_addr_parse("192.168.1.1", &addr) != QL_OK) {
    /* 不是地址 */
}
```

接受的形式恰好是四个十进制八位组，以点分隔，每个取值 0 到 255，且它们之前、
之后与之间都没有任何其他内容：

| 输入 | 结果 |
|---|---|
| `0.0.0.0`, `255.255.255.255`, `10.20.30.40` | `QL_OK` |
| `1.2.3` , `1.2.3.4.5`, `1..3.4`, `1.2.3.4 ` | `QL_ERR_INVALID_ARG` |
| `1.2.3.256`, `999.1.1.1` | `QL_ERR_OUT_OF_RANGE` |
| `::1`, `2001:db8::1` | `QL_ERR_NOT_SUPPORTED` |
| `01.2.3.4`, `010.0.0.1` | `QL_ERR_INVALID_ARG` |

其中两行值得说明一下。

**超过 255 的八位组是 `QL_ERR_OUT_OF_RANGE`，而不是语法错误。** 这样就能把"你
写的东西不是地址"与"你写的地址中某个八位组无法表示"区分开；在记录坏掉的配置
文件时，这通常是更有用的区分。

**前导零会被拒绝，而不是被猜测。** `010` 在十进制中是十，在八进制中是八，现实
世界中这一类安全漏洞都源于解析器选错了那一个。请写 `10.0.0.1`。单独一个 `0`
没有歧义，仍然合法。

**IPv6 是 `QL_ERR_NOT_SUPPORTED`。** 这类文本会被识别为 IPv6，而不是被报告为
格式错误的 IPv4 地址，因此调用方可以区分"尚未支持"和"写错了"。

成功时会写入整个目标对象，包括未使用的字节和 `reserved`，因此无论调用方的变量
此前装的是什么，结果都是确定的。

## 格式化

```c
char buf[QL_NET_ADDR_STR_MAX];
if (ql_net_addr_format(&addr, buf, sizeof buf) != QL_OK) {
    /* 缓冲区太小，或者不是 IPv4 地址 */
}
puts(buf);   /* "192.168.1.1" */
```

`QL_NET_ADDR_STR_MAX` 为 46，按 IPv6 也够用来设定，因此这个大小的缓冲区永远不
需要再改。IPv4 只需要 16。

使用调用方提供的缓冲区，而不是返回静态缓冲区：这能让函数保持线程安全，也让生
命期一目了然。

失败时缓冲区**不被触碰**，因此调用方不会把一次部分写入误当成结果。缓冲区小于
地址所需的大小是 `QL_ERR_OUT_OF_RANGE`；格式化 `QL_NET_AF_UNSPEC` 是
`QL_ERR_INVALID_ARG`，因为"无地址"没有文本；格式化 `QL_NET_AF_INET6` 是
`QL_ERR_NOT_SUPPORTED`。

## 比较

```c
if (ql_net_addr_equal(&a, &b)) {
    /* 地址族相同，且地址字节相同 */
}
```

只比较地址族实际使用的那些字节，因此填充永远不会影响结果。地址族是身份的一部
分：同样的字节分别标记为 IPv4 和 IPv6 时不相等。`NULL` 不等于任何东西，甚至不
等于另一个 `NULL`。

## 完整示例

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_net_addr_t addr;
    char text[QL_NET_ADDR_STR_MAX];

    if (ql_net_addr_parse("203.0.113.7", &addr) != QL_OK) {
        return 1;
    }
    if (ql_net_addr_format(&addr, text, sizeof text) != QL_OK) {
        return 1;
    }
    printf("%s family=%d\n", text, (int)addr.family);

    if (ql_net_addr_equal(&addr, &addr)) {
        puts("equal to itself");
    }
    return 0;
}
```

```
203.0.113.7 family=1
equal to itself
```

## 错误码

| 情况 | 错误码 |
|---|---|
| `NULL` 参数 | `QL_ERR_INVALID_ARG` |
| 格式错误的文本：八位组个数不对、空八位组、非数字字符、尾部多余字符、前导零 | `QL_ERR_INVALID_ARG` |
| 八位组超过 255 | `QL_ERR_OUT_OF_RANGE` |
| 格式化地址族为 `QL_NET_AF_UNSPEC` 的地址 | `QL_ERR_INVALID_ARG` |
| 格式化未知地址族的地址 | `QL_ERR_INVALID_ARG` |
| 缓冲区小于地址所需的大小 | `QL_ERR_OUT_OF_RANGE` |
| 文本是 IPv6，或该地址是 IPv6 | `QL_ERR_NOT_SUPPORTED` |

以上每一种情况也都会上报给已安装的[错误回调](errors.md)，并带有一条指名该函数
的消息。
