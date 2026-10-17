# ABI 与版本管理

## 这里的 ABI 稳定性意味着什么

头文件与编译出来的库是独立进行版本管理的：程序针对某一组头文件编译，并链接到某个 `libqlib` 构建。ABI 稳定性意味着，针对较旧库构建的二进制无需重新编译即可与较新的库一起工作，只要主版本号未变。

在本代码库中，有三条性质使之成为可能：

| 性质 | 在何处被保证 |
|---|---|
| 调用方从不计算库所拥有对象的大小或布局 | `ql_vector_t` 是不透明类型；`struct ql_vector` 只存在于 `src/ql_vector.c`（见[架构](architecture.md)） |
| 暴露的值结构体可以在不移动已有成员的前提下增长 | [`ql_mem.h`](../../../include/qlib/ql_mem.h) 中的 `ql_allocator_t.reserved[4]`（见[设计决策](design-decisions.md)） |
| 已有枚举值永不改变其数值 | `ql_ret_t` 是只追加的 |

## `ql_ret_t` 的只追加规则

`ql_ret_t` 是一个从 `QL_OK = 0` 开始隐式编号的普通枚举：

```c
typedef enum
{
    QL_OK = 0,
    QL_ERR_NOMEM,         /* 1 */
    ...
    QL_ERR_OVERFLOW,      /* 13 */
} ql_ret_t;
```

头文件直接陈述了这条规则：*"新错误码只会被追加。数值是 ABI 的一部分，不得重排或重新编号。"*

这条规则之所以重要，是因为该值以 `int` 的形式跨越边界。重新编号 `QL_ERR_INVALID_ARG`，或者在中间插入一个新错误码，会静默地改变调用方已经存储、比较或记录过的每一个 `ql_ret_t` 的含义。追加则让已有二进制保持正确，只是让它们不认识那些从未被编译进来期待过的错误码——这正是 `ql_strerror()` 必须对所有取值都有定义的原因（见[设计决策](design-decisions.md) 中的 ADR-7）。

## 符号导出

### `QLIB_EXPORT`

每个公共函数都带有 `QLIB_EXPORT`，在头文件的声明处施加。[CONTRIBUTING.md](../../../CONTRIBUTING.md) 规则 9 规定这对新增公共函数是强制性的。

该宏来自 CMake 的 `GenerateExportHeader` 模块，在 [CMakeLists.txt](../../../CMakeLists.txt) 中配置：

```cmake
generate_export_header(qlib
    BASE_NAME QLIB
    EXPORT_MACRO_NAME QLIB_EXPORT
    EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/qlib/qlib_export.h"
)
```

生成出来的文件是**构建目录中**的 `include/qlib/qlib_export.h`——它不存在于源码树中，而每个公共头文件都会包含它。有一个值得知道的后果：只带 `-Iinclude` 让编译器指向源码树会失败，因为生成的头文件不在那里。`include/` 与 `<build>/include/` 两者都需要，而这正是构建接口与安装接口所提供的东西：

| 使用方 | 包含目录 |
|---|---|
| 构建目录 | `$<BUILD_INTERFACE:.../include>` 与 `$<BUILD_INTERFACE:.../<build>/include>` |
| 已安装 | `$<INSTALL_INTERFACE:include>`，并且 `qlib_export.h` 会与其他头文件一起被安装 |

### `QLIB_STATIC_DEFINE`

在 Windows 上，静态库与 DLL 需要相反的声明：使用 DLL 需要 `__declspec(dllimport)`，使用静态库则什么都不需要。生成的头文件用 `QLIB_STATIC_DEFINE` 解决这一点：

```cmake
# 静态构建在 Windows 上不得把 QLIB_EXPORT 展开为 dllimport。
if(NOT QLIB_SHARED)
    target_compile_definitions(qlib PUBLIC QLIB_STATIC_DEFINE)
endif()
```

`PUBLIC` 意味着该定义会传播：任何在构建目录中链接 `qlib::qlib` 的东西都会自动获得它。已在本检出上验证——在默认（静态）构建中，测试目标使用 `-DQLIB_STATIC_DEFINE` 编译。

### ELF 与 PE

同一个 `QLIB_EXPORT` 记号在不同目标文件格式下展开结果不同。

| | ELF（Linux、macOS） | PE（Windows/MSVC） |
|---|---|---|
| 构建库时 | `__attribute__((visibility("default")))` | `__declspec(dllexport)` |
| 使用库时 | `__attribute__((visibility("default")))` | `__declspec(dllimport)` |
| 静态使用 | 展开为空（`QLIB_STATIC_DEFINE`） | 展开为空 |
| 选择宏 | 不需要 | `qlib_EXPORTS`，由 CMake 在编译 `SHARED` 目标时定义 |

在 ELF 上，两个分支除了注释之外在文本上完全相同，因为默认已经是公共可见性——[CMakeLists.txt](../../../CMakeLists.txt) 中任何地方都没有设置 `C_VISIBILITY_PRESET`。实际效果是：在 Linux 上让符号可见的并不是 `QLIB_EXPORT`；在 Windows 上让符号可见的才是它。因此像 `ql_err_raise()` 这样的内部辅助函数在 ELF 上*并不*隐藏，尽管它们不在 Windows 导入库中。已验证：在共享构建上执行 `nm -D`，会把 `ql_err_raise` 与公共 API 一并列出。

由此得到一条不变量：只有标记了 `QLIB_EXPORT` 的声明才受本文档中兼容性承诺的约束。

## 版本号

存在两套彼此独立的版本机制，它们回答不同的问题。

| 机制 | 来源 | 它回答的问题 |
|---|---|---|
| `VERSION 0.2.0` / `SOVERSION 0` | [CMakeLists.txt](../../../CMakeLists.txt) 中的 `project(qlib VERSION 0.2.0 ...)` | 加载器会选择哪个二进制？ |
| `QLIB_VERSION_MAJOR/MINOR/PATCH/STRING` | [`ql_common.h`](../../../include/qlib/ql_common.h) | 这个翻译单元是针对哪组头文件编译的？ |
| `ql_version()` | `src/ql_common.c`，返回 `QLIB_VERSION_STRING` | 运行时实际加载的是哪个库？ |

`VERSION` 与 `SOVERSION` 会成为共享对象的 ELF 版本信息。在本检出上，一次共享构建产生：

```text
libqlib.so -> libqlib.so.0 -> libqlib.so.0.2.0
SONAME: libqlib.so.0
```

`SOVERSION` 是 `${PROJECT_VERSION_MAJOR}`，因此加载器要求主版本号为 0，并接受任何存在的 0.x 库。

### 宏与 `ql_version()`

`QLIB_VERSION_*` 宏被烘焙进翻译单元编译时所用的那组头文件中。`ql_version()` 报告的则是运行时加载的库。当程序、头文件和库都来自同一次构建时，二者一致。它们在不一致的那种要紧情形下确实不一致：应用程序针对新的头文件编译，却从过期的安装路径加载了较旧的 `libqlib.so`，或者反过来。

由于库只能承诺次版本升级在单方向上是兼容的，启动时显式做这项检查是值得的：

```c
if (strcmp(ql_version(), QLIB_VERSION_STRING) != 0) {
    /* 头文件与库并非来自同一次构建 */
}
```

头文件对此有文档说明。注意 `ql_version()` 返回 `const char*`，因此该比较是字符串比较而非数值比较——当程序需要根据版本特性分支时，`QLIB_VERSION_MAJOR` 及其同类才是数值形式。

## CMake 包版本

CMake 包记录的是一项兼容性策略，而不只是一个数字：

```cmake
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/qlibConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
```

`SameMajorVersion` 意味着使用方的 `find_package(qlib 0.1 REQUIRED)` 接受任何已安装的 `0.x.y`，并拒绝 `1.x`。已验证的行为：安装 `0.2.0` 后，`find_package(qlib 0.1 REQUIRED)` 成功；请求 `9.9` 会失败并报 *"Could not find a configuration file for package "qlib" that is compatible with requested version "9.9""*。解析器报告的已安装版本是 `0.2.0`。

只有在每次不兼容变更都会提升主版本号的前提下，`SameMajorVersion` 才是这里正确的策略。鉴于版本仍是 `0.x`，更严格的解读是：`1.0.0` 之前的**任何**一次发布都可能破坏兼容性，而包版本检查不会发现这一点，因为所有 `0.x` 版本互相都可接受。

## 次版本中允许变更的内容

在不改变主版本号的前提下允许：

- 新增函数、新增头文件、新增不透明类型。
- 向 `ql_ret_t` 末尾追加新的枚举项。
- 向私有（不透明）结构体的定义中添加新字段。
- 占用 `ql_allocator_t.reserved[]` 中的一个槽位，前提是已有成员保持它们的位置，且 `ql_default_allocator()` 会填充新槽位。
- 修改任何实现细节、增长策略或内部文件布局。

不允许：

- 删除或重命名公共函数，或修改它的形参列表或返回类型。
- 修改任何 `ql_ret_t` 枚举项的数值，或者在末尾之外的任何位置插入新枚举项。
- 重排或改变 `ql_allocator_t` 的大小，或删除成员——包括 `reserved[]`。
- 把原本完整的类型变为不透明，或暴露当前不透明类型的布局。
- 修改有文档说明的所有权或生命周期规则，例如 `ql_realloc()` 中的"失败时原块仍归调用方所有"。

对 `ql_allocator_t` 大小或布局的修改，或删除任何导出函数，都属于主版本变更。对于调用方能够通过某项有文档说明的保证观察到的任何行为，即便签名未变，也是如此。
