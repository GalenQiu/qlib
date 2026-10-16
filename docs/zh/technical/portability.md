# 可移植性

## 支持矩阵

CI 会构建并测试的平台/编译器组合，对比仅仅预期可以工作的组合。

| 平台 | 编译器 | 状态 | 覆盖方式 |
|---|---|---|---|
| Linux | GCC | 支持 | `linux / gcc` job、`ubuntu-24.04`、`-DQLIB_WERROR=ON` |
| Linux | Clang | 支持 | `linux / clang` job |
| Linux | Clang + ASAN/UBSAN | 支持 | `sanitizers` job、`-DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` |
| Linux | Clang + libFuzzer | 支持 | `fuzz` job、`-DQLIB_BUILD_FUZZ=ON` |
| macOS | AppleClang | 支持 | `macos` job、`macos-latest` |
| Windows | MSVC | 支持 | `windows` job、静态与共享两套矩阵 |
| Windows | MinGW / clang-cl | 未测试 | — |
| 其他任意 Unix | GCC / Clang | 未经测试 | 预期可以工作：目前除 `errno` 常量之外不存在任何平台相关代码 |

尽管项目的主打描述只提到 Linux 和 Windows，macOS 仍由 `macos` job 覆盖；实际执行了哪些内容，以 CI job 为准。

## C11 基线

基线是禁用编译器扩展的 C11。[CONTRIBUTING.md](../../../CONTRIBUTING.md) 的规则 1 这样规定，[CMakeLists.txt](../../../CMakeLists.txt) 则强制实施：

```cmake
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 使用方还额外继承 C11 下限。
target_compile_features(qlib PUBLIC c_std_11)
```

前三行分别是 `C_STANDARD`、`C_STANDARD_REQUIRED` 和 `C_EXTENSIONS` 这三个目标属性的目录级默认值，因此它们同样覆盖库、测试和模糊测试目标。正是 `C_STANDARD_REQUIRED` 与 `C_EXTENSIONS OFF` 的组合要求使用 `-std=c11` 而不是 `-std=gnu11`。

已从 `compile_commands.json` 验证：每个第一方目标都只用 `-std=c11` 编译，没有别的标准选项，并且在该模式下整棵代码树能在 `-Wpedantic -Werror` 下干净构建。

两种机制同时存在，因为它们承担不同的职责：

| 机制 | 效果 |
|---|---|
| `CMAKE_C_STANDARD` + `C_STANDARD_REQUIRED` + `C_EXTENSIONS OFF` | 把 qlib 自身的目标精确固定为 `-std=c11` |
| `target_compile_features(qlib PUBLIC c_std_11)` | 对任何链接 `qlib::qlib` 的一方要求 C11 *最低*标准，但不会把某个方言强加给使用方 |

仅靠 `target_compile_features()` 并不够：它表达的是下限，因此默认方言已经满足 `c_std_11` 的编译器根本收不到 `-std=` 选项，而是按自己的默认方言构建——例如 GCC 16 上的 C23。在显式固定方言之前，本仓库正是这种状态。

代码不得依赖比 C11 更新的特性，也不得依赖编译器扩展。唯一一处刻意放宽的是 [`ql_common.h`](../../../include/qlib/ql_common.h)：当编译器提供更新的写法时，`QLIB_NODISCARD` 可以使用它们——下面的降级链保证在严格的 C11 编译器上展开为空。GCC 和 Clang 在 `-std=c11` 模式下接受 `__attribute__`，因此属性这条路径在固定方言下可以编译。

## `QLIB_NODISCARD` 降级链

声明于 [`ql_common.h`](../../../include/qlib/ql_common.h)，并附加到每一个结果不得被忽略的函数上——分配辅助函数以及会修改内容的 vector 操作。

```c
#if defined(__cplusplus) && __cplusplus >= 201703L
#  define QLIB_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#  define QLIB_NODISCARD __attribute__((warn_unused_result))
#elif defined(_Check_return_)
#  define QLIB_NODISCARD _Check_return_
#else
#  define QLIB_NODISCARD
#endif
```

| 顺序 | 守卫 | 展开结果 | 说明 |
|---|---|---|---|
| 1 | `__cplusplus >= 201703L` | `[[nodiscard]]` | C++17 及以后。 |
| 2 | `__GNUC__` 或 `__clang__` | `__attribute__((warn_unused_result))` | 所有 C 编译走的路径，包括 clang-cl，因为它也定义了 `__clang__`。 |
| 3 | `_Check_return_` | `_Check_return_` | MSVC，且仅在别的什么东西已经包含过 SAL 时才成立——该宏用 `#ifdef` 测试，而不是假定其存在。 |
| 4 | — | 空 | 其他任何编译器。代码仍能编译；只是丢失了诊断信息。 |

这一排序带来两个局限。C 编译时*不会*检测到 C23 的 `[[nodiscard]]`——没有 `__STDC_VERSION__` 分支——因此 C23 编译器会落到 GCC/Clang 属性这条路径，效果等价。另外，这条链只在编译期起作用；`QLIB_NODISCARD` 产生的是警告，只有 `-Werror` 或 `/WX` 生效时才会变成构建失败，在本项目里也就是 `-DQLIB_WERROR=ON` 或 CI。

在 GCC 上验证过的行为：忽略 `ql_alloc(NULL, 4);` 会产生 `warning: ignoring return value of 'ql_alloc' declared with attribute 'warn_unused_result' [-Wunused-result]`。

## `ql_ret_from_errno()` 与 `#ifdef` 守卫的分支

`ql_ret_from_errno()` 把平台错误码映射为 `ql_ret_t`。它位于 `src/ql_common.c`，是库中唯一包含 `<errno.h>` 的地方。

POSIX 的 `errno` 集合比 MSVC 宽得多。因此有六种情况需要加守卫，因为这些常量可能根本没有定义：

| 无条件 | 用 `#ifdef` 守卫 |
|---|---|
| `0`、`ENOMEM`、`EINVAL`、`ERANGE`、`ENOENT`、`EIO` | `EEXIST`、`EAGAIN`、`ETIMEDOUT`、`EBUSY`、`EOVERFLOW`、`ENOTSUP` |

任何不匹配任何分支的错误码都会落到 `QL_ERR_SYS`，因此未映射的错误仍会被报告，而不是被丢弃。

还有第二个约束决定了这个列表：**刻意不使用别名。** 在 glibc 上 `EWOULDBLOCK == EAGAIN`、`EOPNOTSUPP == ENOTSUP`。把同一个别名的两种拼写都写进一个 `switch` 会产生两个值相同的 `case` 标签，C 会将其视为编译错误而拒绝。`src/ql_common.c` 中的注释记录了这一点，以免把这种省略误认为疏漏。既然别名是同一个整数，主名称已经能匹配该别名可能取到的每一个值。

## 不使用 Annex K（`*_s`）函数

C11 标准定义了一个可选的带边界检查的库，即 Annex K，提供 `memcpy_s`、`memset_s`、`memmove_s` 等函数。本项目不使用它，[`.clang-tidy`](../../../.clang-tidy) 中相应的 clang-tidy 检查也因此被禁用，并注明了理由。

Annex K 是可选的，glibc 和 macOS libc 都不提供它。在 `src/ql_vector.c` 中改用 `memcpy_s` 会破坏 CI 支持的三个平台中的两个，而且不存在不会重复实现该检查的可移植垫片。因此 `ql_vector.c`、`ql_mem.c` 和 minunit 测试框架中的普通 `memcpy`、`memmove`、`memset` 调用才是预期使用的工具。

## 为什么移除 `ql_lock_t`

更早的版本在 `ql_common.h` 中声明了一个锁适配器（`ql_lock_t`、`ql_lock_fn`、`ql_unlock_fn`）。它被删除而不是被实现，原因之一正是可移植性：

- 可用的实现需要两条代码路径——POSIX 线程和 Win32 `CRITICAL_SECTION`。
- 这种东西的测试必须启动真实线程。一个可移植的测试就需要 C11 `<threads.h>`，而 **macOS 不提供它**；因此测试还得在实现已有的两条平台路径之上再加两条自己的平台路径。
- 目前还不存在会使用它的并发容器。

只发布声明而不实现的做法被否决了，因为未经测试的跨平台线程代码比缺失的功能代价更高。完整理由见[设计决策](design-decisions.md)的 ADR-12；`ql_allocator_t.reserved[4]` 是为它回归预留的空间。

## 警告选项

通过 `qlib_warnings` 接口目标施加，每个第一方目标都会链接它（见[构建系统](build-system.md)）。

| 编译器 | 选项 |
|---|---|
| GCC、Clang、AppleClang | `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wwrite-strings` |
| MSVC | `/W4` |
| 任意编译器，当 `QLIB_WERROR=ON` 时 | GCC/Clang：`-Werror`；MSVC：`/WX` |

测试和模糊测试入口之所以显式声明原型，原因就是 `-Wmissing-prototypes` 和 `-Wstrict-prototypes`：`LLVMFuzzerTestOneInput` 在 `fuzz/fuzz_vector.c` 中前置声明，测试套件函数则通过 `tests/test_suites.h` 中的 `MU_DECLARE_SUITE` 声明。

`QLIB_WERROR` 默认为 `OFF`。[CMakeLists.txt](../../../CMakeLists.txt) 的开头没有把它写死，因此使用方用更新的编译器构建时，不会因为不是自己造成的警告而被挡住。CI 在每个构建 job 中都设置 `-DQLIB_WERROR=ON`。

## Windows 上非 CMake 用户的 `QLIB_STATIC_DEFINE`

生成的 `qlib_export.h` 依据 `QLIB_STATIC_DEFINE` 选择 `__declspec(dllexport)` / `__declspec(dllimport)` / 什么都不加。CMake 使用方会自动得到正确结果：静态目标把 `QLIB_STATIC_DEFINE` 作为 `PUBLIC` 编译定义加入，因此任何链接 `qlib::qlib` 的一方都会收到它。

在 CMake 之外没有这种便利。在 Windows 上用 MSVC 手动或借助其他构建系统针对**静态**库编译的用户，必须自己定义它：

```bat
cl /DQLIB_STATIC_DEFINE /I<prefix>\include app.c <prefix>\lib\qlib.lib
```

不定义它的话，头文件会把 `QLIB_EXPORT` 展开为 `__declspec(dllimport)`，链接就会失败。README 中“使用 qlib”一节也提到了这一点。**DLL** 的使用方则不得定义它。

## Sanitizer

`QLIB_SANITIZE=ON` 以目录级作用域施加选项，使其覆盖项目中的每个目标，包括测试和模糊测试目标：

| 编译器 | 编译与链接选项 |
|---|---|
| GCC / Clang | `-fsanitize=address,undefined -fno-omit-frame-pointer -g` |
| MSVC | 仅 `/fsanitize=address` |

MSVC 自带 AddressSanitizer，但没有 UndefinedBehaviorSanitizer，因此那里的 `QLIB_SANITIZE` 弱于另外两个平台。在 Linux 上，ASAN 还会带来 LeakSanitizer，所以那个 CI job 被称为 `asan+ubsan`，并且无需额外配置就能捕获泄漏。
