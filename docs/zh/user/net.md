# 网络地址

`<qlib/ql_net.h>` 把文本形式的 IPv4 与 IPv6 地址转换为值，并能反向转换。它是
网络模块的第一块切片，也是刻意选择的那一块：不需要 socket、不需要平台初始化、
也不需要外部库——它只是对字符串做纯计算，因此在 qlib 支持的每个平台上行为一致，
测试时也不需要网络。

地址处理不需要网络、不需要平台初始化、也不需要链接任何外部库。TCP socket
三样都需要，因此它是下面独立的一节，而地址那一半在没有它时照常工作。

DNS 可用；IPv6 *socket* 不可用，这是刻意的——原因见下面的 **TCP socket** 一节。

> 该模块是可选模块。以 `QLIB_BUILD_NET=OFF` 配置的构建会把它整体省略，此时
> `<qlib/ql_net.h>` 也不会被安装。两种配置下包含 `<qlib/qlib.h>` 都能正常工作。
> 参见[集成](integration.md)。

## 地址类型

```c
typedef enum {
    QL_NET_AF_UNSPEC = 0,   /* 无地址；零值 */
    QL_NET_AF_INET,         /* IPv4 */
    QL_NET_AF_INET6         /* IPv6：已解析和格式化，但不可连接 */
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

**冒号意味着 IPv6**，而 IPv6 会被解析。参见下一节。

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

`QL_NET_ADDR_STR_MAX` 为 46，这正是 IPv6 所需的长度，因此这个大小的缓冲区足以
覆盖两个地址族。仅 IPv4 只需要 16，而对 IPv4 地址来说更短的缓冲区也会被接受。

使用调用方提供的缓冲区，而不是返回静态缓冲区：这能让函数保持线程安全，也让生
命期一目了然。

失败时缓冲区**不被触碰**，因此调用方不会把一次部分写入误当成结果。缓冲区小于
地址所需的大小是 `QL_ERR_OUT_OF_RANGE`；格式化 `QL_NET_AF_UNSPEC` 是
`QL_ERR_INVALID_ARG`，因为"无地址"没有文本；格式化 `QL_NET_AF_INET6` 是
`QL_ERR_NOT_SUPPORTED`。

## IPv6 地址

接受的形式包括八组写法、`::` 压缩（最多一次），以及末尾的点分四组形式（例如
`::ffff:192.0.2.1`）。区域标识符（`%eth0`）会被拒绝：本模块没有可以承载
它们的接口索引。

```c
ql_net_addr_t addr;
char text[QL_NET_ADDR_STR_MAX];

ql_net_addr_parse("2001:0db8:0000:0000:0000:0000:0000:0001", &addr);
ql_net_addr_format(&addr, text, sizeof text);
puts(text);   /* 2001:db8::1 */
```

格式化遵循 RFC 5952，这也是其他工具会生成并期待的形式：

| 规则 | 示例 |
|---|---|
| 小写，且无前导零 | `2001:0DB8::0001` 格式化为 `2001:db8::1` |
| **两个或更多**零组构成的最长连续段会被压缩 | `2001:db8::1` |
| 单个零组永不压缩 | `2001:db8:0:1:1:1:1:1` |
| 并列时**最靠左**的连续段胜出 | `2001:db8::1:0:0:1` |
| IPv4 映射地址使用混合记法 | `::ffff:192.0.2.1` |

最后一条规则出自 RFC 5952 §5： `::ffff:c000:201` 是同一个地址，但点分四组才是
人能够读懂的形式。

**socket 不接受 IPv6。** 对于 IPv6 地址，`ql_net_tcp_connect()` 与
`ql_net_tcp_listen()` 会报告 `QL_ERR_NOT_SUPPORTED`，而不是悄悄去做别的事情。
地址处理属于字符串问题；IPv6 socket 是另一项独立的平台工作。

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

## TCP socket

socket 只用 IPv4，是阻塞的，而且没有 DNS：用 `ql_net_addr_parse()` 构造一个
`ql_net_addr_t`，再连接到它。整个设计刻意就到此为止。非阻塞 I/O 要么需要线程、
要么需要事件循环，而 qlib 两者都还没有；域名解析需要一套关于超时与取消的策略，
而本库无法兑现。这两者都是不提供，而不是只做一半。

| 函数 | 用途 |
|---|---|
| `ql_net_tcp_listen` | 绑定并监听；端口传 0 由系统选择 |
| `ql_net_tcp_connect` | 连接到正在监听的 socket |
| `ql_net_accept` | 取出下一个待处理的连接 |
| `ql_net_send` / `ql_net_recv` | 搬运字节 |
| `ql_net_local_port` | 读回已绑定的端口 |
| `ql_net_set_timeout` | 为阻塞等待设定上限，单位为毫秒 |
| `ql_net_socket_close` | 关闭并释放一个 socket |

socket 要求平台的 socket 层已经启动，在 Windows 上这意味着 `WSAStartup()`。每个
socket 函数都会自己调用 `ql_net_init()`，因此这件事已经替你处理好了；在启动时显式
调用它，只是把一次平台失败挪到你能够掌控的位置。`ql_net_shutdown()` 是可选的。

### 回环往返

```c
#include <qlib/ql_net.h>
#include <stdio.h>

int main(void)
{
    ql_net_addr_t loopback;
    ql_net_socket_t* listener = NULL;
    ql_net_socket_t* client = NULL;
    ql_net_socket_t* server = NULL;
    uint16_t port = 0;
    size_t moved = 0;
    char buffer[16];

    if (ql_net_addr_parse("127.0.0.1", &loopback) != QL_OK) { return 1; }
    if (ql_net_tcp_listen(NULL, &loopback, 0, 4, &listener) != QL_OK) { return 1; }
    if (ql_net_local_port(listener, &port) != QL_OK) { return 1; }

    if (ql_net_tcp_connect(NULL, &loopback, port, &client) != QL_OK) { return 1; }
    if (ql_net_accept(listener, &server) != QL_OK) { return 1; }

    if (ql_net_send(client, "ping", 4, &moved) != QL_OK) { return 1; }
    if (ql_net_recv(server, buffer, sizeof buffer, &moved) != QL_OK) { return 1; }

    printf("server read %zu bytes\n", moved);

    ql_net_socket_close(server);
    ql_net_socket_close(client);
    ql_net_socket_close(listener);
    return 0;
}
```

```
server read 4 bytes
```

在端口 0 上监听、再把端口读回来，是测试在不硬编码端口的前提下取得端口的办法，
该模块自己的测试正是这么做的。

### 查询域名

```c
ql_net_addr_t addr;

if (ql_net_addr_resolve("example.com", &addr) != QL_OK) {
    /* 域名未能解析 */
}
if (ql_net_tcp_connect(NULL, &addr, 443, &client) != QL_OK) {
    /* ... */
}
```

`ql_net_addr_resolve()` 走平台的解析器，因此 `/etc/hosts`、DNS 配置以及平台自己的缓存都会生效。数字地址会被接受，且无需任何查询，这也是在完全没有网络时唯一可用的形式。

只考虑 IPv4 结果：一个只有 AAAA 记录的主机会被报告为未找到，而不是用一个本模块并未实现的地址族去连接。

解析失败是答案，而不是故障：

| 返回值 | 含义 |
|---|---|
| `QL_ERR_NOT_FOUND` | 该域名无法解析 |
| `QL_ERR_AGAIN` | 解析器暂时无法工作；重试可能成功 |
| `QL_ERR_SYS` | 解析器中出了别的差错 |

### 三条容易弄错的规则

**短写不是错误。** 内核可能只接受比你给出的更少的字节；`out_sent` 会说明接受了
多少。用剩下的部分再次调用即可。把 `out_sent < len` 当作失败，会在负载下丢失
数据。

**收到零字节意味着对端干净地关闭了连接。** 在流结束时，`ql_net_recv` 返回 `QL_OK`，
且 `*out_received == 0`。这是 POSIX 的约定，不是失败；socket 循环正是以此作为终止
条件。

**超时在任何平台上都是 `QL_ERR_TIMEOUT`。** 这一点值得说明，因为它并非免费：
最直观的实现是设置 `SO_RCVTIMEO` 交给内核，而它在 Linux 与 macOS 上返回 `EAGAIN`
（无法与发送缓冲区已满区分），在 Windows 上返回 `WSAETIMEDOUT`，而且**在 Windows 上
它根本无法可靠地中断 `accept()`**。qlib 改为用 `select()` 自己做等待，因此一个超时
在所有平台上都是同一个错误码。

`ql_net_tcp_listen` 会设置 `SO_REUSEADDR`，因此重启服务器时不会撞上它先前那些连接
遗留的 `TIME_WAIT`。

### 代价

TCP 这一半是本库**唯一的链接依赖**：Windows 上的 `ws2_32`。这就是为什么整个模块都
放在 `QLIB_BUILD_NET` 之后，也是为什么地址这一半被记录为完全不需要任何东西。以
`QLIB_BUILD_NET=OFF` 构建时，既没有 socket 代码，也没有依赖。

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
