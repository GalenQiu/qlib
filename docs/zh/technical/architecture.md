# 架构

## 目录布局

| 路径 | 作用 |
|---|---|
| [`include/qlib/`](../../../include/qlib) | 公共头文件。这是唯一会被安装的目录。 |
| [`src/`](../../../src) | 库实现。 |
| [`tests/`](../../../tests) | 单元测试框架与测试套件。 |
| [`fuzz/`](../../../fuzz) | libFuzzer 目标。 |
| [`cmake/`](../../../cmake) | 供 `configure_file()` 与 `configure_package_config_file()` 使用的模板。 |
| [`docs/`](../../../docs) | 用户指南、技术文档与 Doxygen 配置。 |
| [`bench/`](../../../bench) | `qlib_bench` 微基准测试套件，由 `QLIB_BUILD_BENCH` 构建。 |
| [`ci/`](../../../ci) | 本地验证：`check.sh`、链接与译文检查器、Win32 语法桩，以及 package job 构建的下游消费方。 |

`include/qlib/qlib_export.h` 由 `generate_export_header()` **生成**到构建目录中，源码树里并不存在。见[构建系统](build-system.md)。

## 分层

```text
qlib/qlib.h                     伞形头文件：包含下面每一个公共头文件
├── ql_common.h                 不依赖任何 qlib 头文件
├── ql_mem.h                    → ql_common.h
├── ql_str.h                    → ql_common.h、ql_mem.h
├── ql_hash.h                   → ql_common.h、ql_mem.h
├── ql_list.h                   → ql_common.h、ql_mem.h
├── ql_vector.h                 → ql_common.h、ql_mem.h
└── ql_net.h                    → ql_common.h          （可选模块）

src/ql_internal.h               私有，不安装
├── ql_common.c                 定义 ql_err_raise()
├── ql_mem.c                    → ql_common.c
├── ql_str.c                    → ql_common.c、ql_mem.c
├── ql_hash.c                   → ql_common.c、ql_mem.c
├── ql_list.c                   → ql_common.c、ql_mem.c
├── ql_vector.c                 → ql_common.c、ql_mem.c
└── ql_net.c                    → ql_common.c
```

依赖方向只有单向：所有容器都依赖 `ql_mem`，`ql_net` 只依赖 `ql_common`（它不分配内存），`ql_mem` 依赖 `ql_common`，而 `ql_common` 除 C 库之外不依赖任何东西。容器之间互不依赖。

| 模块 | 拥有 | 不拥有 |
|---|---|---|
| `ql_common` | `ql_ret_t`、`ql_strerror()`、`ql_ret_from_errno()`、错误回调注册表（`ql_set_error_handler()` / `ql_get_error_handler()`）、版本宏与 `ql_version()`、`QLIB_NODISCARD` | 任何分配。`src/ql_common.c` 包含私有头文件与 `<errno.h>`，不包含任何分配器。 |
| `ql_mem` | `ql_allocator_t` 及其函数指针类型、`ql_default_allocator()`、分配辅助函数（`ql_alloc`、`ql_calloc`、`ql_realloc`、`ql_dealloc`、`ql_alloc_array`、`ql_realloc_array`）、`ql_size_mul_overflow()` | 错误码本身；它调用 `ql_common` 来上报错误。 |
| `ql_vector` | `ql_vector_t` 与 `struct ql_vector` 的布局、增长策略、元素复制/搬移逻辑 | 分配策略；每一个字节都经由存储在 vector 中的分配器。 |
| `ql_str` | `ql_str_t`、维持其 NUL 结尾的增长策略，以及追加与格式化路径 | 其他一概不管：它只存字节，不了解任何文本编码。 |
| `ql_hash` | `ql_hash_t`、FNV-1a 哈希、探测与墓碑策略，以及重哈希 | 键的含义。它用 `memcmp` 比较字节，所以带填充字节的结构体键是调用方自己的问题。 |
| `ql_list` | `ql_list_t`、`ql_list_node_t`，以及链接、解链与节点分配 | 稳定句柄正是它的价值；需要按下标访问的场合应该用 `ql_vector`。 |
| `ql_net` | `ql_net_addr_t` 与 `ql_net_family_t`、文本 IPv4 的解析、格式化与比较 | 任何分配（它不分配内存），以及任何需要 socket、域名解析或平台初始化的东西。 |

两条规则保证分层不被破坏：

1. 模块可以包含位于其下层的模块的头文件，绝不能包含其上层的。
2. 每一次分配都经由存储在对象中的 `ql_allocator_t` 完成。除 `src/ql_mem.c` 之外没有直接的 `malloc()`（见 [CONTRIBUTING.md](../../../CONTRIBUTING.md) 规则 7）。

## 公共头文件图

每个公共头文件都是自包含的：单独包含它便足以使用其中的声明。

| 头文件 | 包含 | 提供 |
|---|---|---|
| [`ql_common.h`](../../../include/qlib/ql_common.h) | `<stddef.h>`、`qlib/qlib_export.h` | 版本、`ql_ret_t`、错误回调 |
| [`ql_mem.h`](../../../include/qlib/ql_mem.h) | `<stddef.h>`、`ql_common.h` | `ql_allocator_t` 与分配辅助函数 |
| [`ql_str.h`](../../../include/qlib/ql_str.h) | `<stdarg.h>`、`<stdbool.h>`、`<stddef.h>`、`ql_common.h`、`ql_mem.h` | `ql_str_t` |
| [`ql_hash.h`](../../../include/qlib/ql_hash.h) | `<stdbool.h>`、`<stddef.h>`、`ql_common.h`、`ql_mem.h` | `ql_hash_t`、`ql_hash_iter_t` |
| [`ql_list.h`](../../../include/qlib/ql_list.h) | `<stdbool.h>`、`<stddef.h>`、`ql_common.h`、`ql_mem.h` | `ql_list_t`、`ql_list_node_t` |
| [`ql_vector.h`](../../../include/qlib/ql_vector.h) | `<stdbool.h>`、`<stddef.h>`、`ql_common.h`、`ql_mem.h` | `ql_vector_t` |
| [`ql_net.h`](../../../include/qlib/ql_net.h) | `<stdbool.h>`、`<stddef.h>`、`ql_common.h` | `ql_net_addr_t`、地址解析与格式化 |
| [`qlib.h`](../../../include/qlib/qlib.h) | 上述全部，其中 `ql_net.h` 仅在定义了 `QLIB_HAS_NET` 时包含 | 伞形便利头文件 |

`ql_net.h` 是可选的：配置为 `QLIB_BUILD_NET=OFF` 的构建既不编译它的源文件，也不安装它，伞形头文件里的条件包含正是为此。机制见[构建系统](build-system.md)。

`ql_vector.h` 包含 `ql_mem.h` 而不是前置声明 `ql_allocator_t`，因为它的若干函数接受 `const ql_allocator_t*`，而调用方需要完整的结构体才能构造一个。每个头文件都用 `extern "C"` 保护包裹其声明，以照顾 C++ 使用方。

## 私有边界

[`src/ql_internal.h`](../../../src/ql_internal.h) 位于 `src/`，从不被安装（`install(DIRECTORY include/qlib ...)` 只复制公共头文件树），并声明了唯一的跨模块辅助函数：

```c
void ql_err_raise(ql_ret_t code, const char* message);
```

`ql_err_raise()` 定义在 `src/ql_common.c` 中，由 `src/ql_mem.c` 与 `src/ql_vector.c` 调用，因此它在链接时必须具有外部链接。它被刻意地**不**标记为 `QLIB_EXPORT`，这在各平台上有不同的后果：

| 平台 | 效果 |
|---|---|
| Windows / PE | 没有 `__declspec(dllexport)`，该符号就不会被放入 DLL 导出表或导入库，因此使用方无法调用它。在 DLL 内部，该调用正常解析。 |
| ELF | 没有设置可见性预设（`C_VISIBILITY_PRESET` 未被配置），因此该符号仍然存在于动态符号表中。于是 `ql_err_raise` 在 Linux/macOS 上可以被意外触达，尽管它并不属于公共 API。 |

所以"未导出"在 Windows 上是由平台强制的契约，在 ELF 上则是一种约定。把 `ql_err_raise()` 视为私有意味着：它不受 [ABI 与版本管理](abi-and-versioning.md) 中版本规则的约束，并且可能在任意一次发布中改变或消失。

## 不透明类型所在的位置

`ql_vector_t` 在公共头文件中是不完整类型；`struct ql_vector` 只在 `src/ql_vector.c` 中定义：

```c
/* ql_vector.h — 调用方可见 */
typedef struct ql_vector ql_vector_t;

/* ql_vector.c — 隐藏 */
struct ql_vector
{
    ql_allocator_t alloc;
    void* data;
    size_t size;
    size_t capacity;
    size_t elem_size;
};
```

调用方持有 `ql_vector_t*`，只能通过访问器触达其内容。该结构体可以增加、删除或重排字段，而不会破坏任何针对该头文件编译过的代码，因为没有任何调用方会计算 `sizeof(struct ql_vector)` 或成员偏移。

有三种暴露的值语义类型是刻意的例外：`ql_allocator_t`、`ql_net_addr_t` 与 `ql_hash_iter_t`。两者都是各自公开头文件中的完整结构体，因为用户必须能够在栈上构造一个；两者都带有尾部 `reserved[]` 成员，以保留同样的增长自由。`ql_net_addr_t` 还存放 16 个地址字节（IPv4 只需要 4 个），这样将来支持 IPv6 就不是 ABI 变更。`ql_hash_iter_t` 暴露出来，是为了让遍历哈希表不产生任何分配。原因见[设计决策](design-decisions.md)。

## 请求流程：`ql_vector_push_back()`

下面的路径是库中每一个会修改状态的操作的模板。

```c
ql_ret_t ql_vector_push_back(ql_vector_t* vec, const void* elem);
```

| # | 步骤 | 位置 | 失败行为 |
|---|---|---|---|
| 1 | 拒绝 `vec == NULL` 或 `elem == NULL` | `src/ql_vector.c` | `ql_err_raise(QL_ERR_INVALID_ARG, ...)`，返回 `QL_ERR_INVALID_ARG` |
| 2 | 询问 `ql_vector_pin_elem()`：`elem` 是否指向 vector 自身存活中的缓冲区 | `ql_vector_contains()` | — |
| 3 | 如果确实构成别名，则分配 `elem_size` 字节作为暂存区，并把元素复制到那里 | `ql_alloc()` | 返回 `QL_ERR_NOMEM`；暂存区被释放 |
| 4 | 如果 `size == capacity`，则增长 | `ql_vector_grow()` | 返回 `QL_ERR_OVERFLOW` 或 `QL_ERR_NOMEM`；vector 不变 |
| 5 | 计算新的容量（倍增，带检查）与字节数 | `ql_size_mul_overflow()` | 若 `capacity * elem_size` 无法放入 `size_t`，则为 `QL_ERR_OVERFLOW` |
| 6 | `ql_realloc(&vec->alloc, vec->data, bytes)` | `src/ql_mem.c` | 为 NULL 时，`ql_err_raise(QL_ERR_NOMEM, ...)` 并返回；`vec->data` 与 `vec->capacity` 保持不动，因此已有元素得以存活 |
| 7 | 解析分配器：`alloc != NULL ? alloc : &ql_default_alloc` | `ql_alloc_resolve()` | — |
| 8 | 调用 `resolved->realloc(ptr, size, resolved->userdata)` | **用户回调** | 用户分配器返回什么就是什么 |
| 9 | 用 `memcpy()` 把被钉住的元素复制到槽位 `size`，然后 `size++` | `src/ql_vector.c` | — |
| 10 | 若曾制作暂存副本，则释放它 | `ql_dealloc()` | — |

步骤 2–3 正是 `ql_vector_push_back(vec, ql_vector_at(vec, 0))` 安全的原因：没有它们，步骤 6 可能交还另一个缓冲区，而 `elem` 仍指向已被释放的那个。相关推理，以及证明这道防护不可或缺的 ASAN 证据，见[设计决策](design-decisions.md)。

步骤 6 还展示了库接触平台内存管理的唯一位置：`ql_default_allocator()` 返回一个 `ql_allocator_t`，其函数指针包装 libc 的 `malloc`/`realloc`/`free`，而其上的每一层只会看到回调表。
