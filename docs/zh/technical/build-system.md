# 构建系统

## 项目布局

| 路径 | 内容 |
|---|---|
| [`CMakeLists.txt`](../../../CMakeLists.txt) | 根项目：选项、sanitizer、警告目标、库目标、子目录、安装/导出规则 |
| [`tests/CMakeLists.txt`](../../../tests/CMakeLists.txt) | `qlib_test` 可执行文件与 `qlib_harness_selftest` 探针 |
| [`fuzz/CMakeLists.txt`](../../../fuzz/CMakeLists.txt) | `fuzz_vector` libFuzzer 目标 |
| [`cmake/qlibConfig.cmake.in`](../../../cmake/qlibConfig.cmake.in) | 供 `find_package(qlib)` 使用的包配置模板 |
| [`cmake/qlib.pc.in`](../../../cmake/qlib.pc.in) | pkg-config 模板 |
| [`cmake/cmake_uninstall.cmake.in`](../../../cmake/cmake_uninstall.cmake.in) | 由 `uninstall` 目标运行的脚本 |
| [`docs/Doxyfile.in`](../../../docs/Doxyfile.in) | `docs` 目标的 Doxygen 配置模板 |

`cmake_minimum_required(VERSION 3.16)`。根文件包含 `GNUInstallDirs`、`CMakePackageConfigHelpers` 和 `GenerateExportHeader`。

## 目标依赖图

```text
qlib_warnings (INTERFACE)          仅构建用的辅助目标，从不安装
      |  PRIVATE $<BUILD_INTERFACE:...>
      v
qlib (STATIC or SHARED)  <---- ALIAS ---->  qlib::qlib
      ^                                        ^
      |  PRIVATE qlib::qlib                     |  PRIVATE qlib::qlib
      |                                        |
  qlib_test                          fuzz_vector        (仅 Clang)
  qlib_harness_selftest  (仅链接 qlib_warnings)
```

| 目标 | 类型 | 定义位置 | 链接 | 输出 |
|---|---|---|---|---|
| `qlib` | 由 `QLIB_SHARED` 决定为 `STATIC` 或 `SHARED` | 根 | `qlib_warnings`（私有，构建接口） | `${PROJECT_BINARY_DIR}/lib` |
| `qlib::qlib` | `ALIAS` | 根 | — | — |
| `qlib_warnings` | `INTERFACE` | 根 | — | — |
| `qlib_test` | 可执行文件 | `tests/` | `qlib::qlib`、`qlib_warnings` | `${PROJECT_BINARY_DIR}/bin` |
| `qlib_harness_selftest` | 可执行文件 | `tests/` | 仅 `qlib_warnings` | `${PROJECT_BINARY_DIR}/bin` |
| `fuzz_vector` | 可执行文件 | `fuzz/` | `qlib::qlib`、`qlib_warnings` | `${PROJECT_BINARY_DIR}/bin` |
| `docs` | 自定义目标 | 根，当 `QLIB_BUILD_DOCS=ON` 时 | — | `docs/html` |

`docs` 是自定义目标，不会被链接进任何东西：它需要用 `cmake --build build --target docs` 显式构建。它需要 Doxygen，并在找到 `dot` 组件时可选地使用 Graphviz。

使用方应始终链接 `qlib::qlib`，绝不要链接 `qlib`。输出目录经过设置，以符合文档中给出的路径：`libqlib.a` / `libqlib.so` 位于 `build/lib` 下，每个可执行文件位于 `build/bin` 下。在多配置生成器（MSVC、Xcode）上，CMake 会追加配置名，因此测试二进制是 `build/bin/Debug/qlib_test.exe`。

注册的测试：

| `add_test` 名称 | 命令 | 注册位置 |
|---|---|---|
| `qlib_common_test` | `qlib_test` | `tests/CMakeLists.txt` |
| `qlib_harness_selftest` | `${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:qlib_harness_selftest> -P tests/selftest/expect_failure.cmake` | `tests/CMakeLists.txt` |
| `fuzz_vector_smoke` | `fuzz_vector -runs=2000 -max_len=128` | `fuzz/CMakeLists.txt`，仅当该目标存在时 |

## 缓存选项

| 选项 | 默认值 | 效果 |
|---|---|---|
| `QLIB_BUILD_TESTS` | 顶层时为 `ON`，否则为 `OFF` | 添加 `tests/` 子目录并调用 `enable_testing()` |
| `QLIB_BUILD_BENCH` | `OFF` | 暂无内容；发出 `WARNING`，说明不存在基准测试 |
| `QLIB_BUILD_FUZZ` | `OFF` | 添加 `fuzz/` 子目录。需要 Clang；否则产生 `WARNING` |
| `QLIB_BUILD_NET` | `OFF` | 暂无内容；发出 `WARNING`，说明该模块尚未实现 |
| `QLIB_SHARED` | `OFF` | 使用 `add_library(qlib SHARED ...)` 而非 `STATIC` |
| `QLIB_WERROR` | `OFF` | 向 `qlib_warnings` 添加 `-Werror`（GCC/Clang）或 `/WX`（MSVC） |
| `QLIB_INSTALL` | `ON` | 启用全部安装/导出/包配置/pkg-config/uninstall 规则 |
| `QLIB_SANITIZE` | `OFF` | 以目录作用域添加 ASAN+UBSAN 选项 |
| `QLIB_BUILD_DOCS` | `OFF` | 需要 Doxygen；配置 `docs/Doxyfile.in` 并添加 `docs` 自定义目标 |
| `QLIB_INSTALL_CMAKEDIR` | `${CMAKE_INSTALL_LIBDIR}/cmake/qlib` | `CACHE STRING`；CMake 包文件的安装位置。定义在 `QLIB_INSTALL` 块内 |

根文件设置的非缓存（普通）变量：

| 变量 | 取值 | 用途 |
|---|---|---|
| `CMAKE_C_STANDARD` / `CMAKE_C_STANDARD_REQUIRED` / `CMAKE_C_EXTENSIONS` | `11` / `ON` / `OFF` | 目标属性的目录级默认值，把每个第一方目标固定为不带扩展的 `-std=c11` |
| `QLIB_C_STANDARD` | `c_std_11` | 传给 `target_compile_features(qlib PUBLIC ...)`，让使用方继承 C11 下限 |
| `QLIB_TESTS_DEFAULT` | `ON`/`OFF` | 计算出的默认值，供 `QLIB_BUILD_TESTS` 选项使用 |
| `QLIB_SRC` | `src/ql_common.c`、`src/ql_mem.c`、`src/ql_vector.c` | 库目标的源文件列表 |
| `DOXYGEN_HAVE_DOT` | `YES`/`NO` | 作为 `HAVE_DOT` 替换进 `Doxyfile.in` |

C 标准被刻意通过两种机制固定。仅靠 `target_compile_features()` 表达的是下限：默认方言已经满足 `c_std_11` 的编译器根本收不到 `-std=` 选项，于是按比如 C23 构建。真正发出 `-std=c11` 的是 `C_STANDARD` 加上 `C_STANDARD_REQUIRED` 和 `C_EXTENSIONS OFF`。参见[可移植性](portability.md)。

顶层判断用源目录比较来完成，而没有用 `PROJECT_IS_TOP_LEVEL`，因为后者需要 CMake 3.21：

```cmake
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(QLIB_TESTS_DEFAULT ON)
else()
    set(QLIB_TESTS_DEFAULT OFF)
endif()
```

这意味着通过 `add_subdirectory()` 引入的使用方只构建库，除非它显式要求构建测试。

## 构建与安装接口

公开包含目录使用生成器表达式的拆分：

```cmake
target_include_directories(qlib PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
```

构建侧出现两个目录，是因为 `include/qlib/qlib_export.h` 是生成到构建目录中的，而其余头文件位于源码树。安装侧只用一个相对路径就够了，因为全部五个头文件最终都落在同一个目录。`$<INSTALL_INTERFACE:...>` 正是用来防止导出机器上的绝对源码路径出现在 `qlibTargets.cmake` 中的。

同样的道理适用于警告选项：

```cmake
target_link_libraries(qlib PRIVATE $<BUILD_INTERFACE:qlib_warnings>)
```

`qlib_warnings` 是仅用于构建的工具：它为本项目携带 `-Wall -Wextra ...`，绝不能成为下游使用方的依赖要求。没有 `$<BUILD_INTERFACE:>` 包装时，CMake 会以 *"target "qlib_warnings" that is not in any export set"* 拒绝配置，因为 `install(EXPORT qlibTargets ...)` 无法描述包并不随附的依赖。加上包装后，这个辅助目标就不会进入导出目标的接口。

出于同样的原因，`install(TARGETS qlib ...)` 没有 `INCLUDES DESTINATION` 子句：`target_include_directories()` 已经贡献了一个 `$<INSTALL_INTERFACE:...>` 条目，两者都指定会让路径在生成的 `INTERFACE_INCLUDE_DIRECTORIES` 中重复。已验证：安装后的 `qlibTargets.cmake` 只包含一个条目，即 `"${_IMPORT_PREFIX}/include"`。

## 导出头文件

```cmake
generate_export_header(qlib
    BASE_NAME QLIB
    EXPORT_MACRO_NAME QLIB_EXPORT
    EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/qlib/qlib_export.h"
)
```

生成的文件落在构建目录中，与其他头文件一起安装，`QLIB_EXPORT` 和 `QLIB_NO_EXPORT` 就由它定义。静态构建时，该目标还会把 `QLIB_STATIC_DEFINE` 作为 `PUBLIC` 定义加入。宏在各平台上展开成什么，见 [ABI 与版本](abi-and-versioning.md)。

## 安装、导出与包配置

以下内容全部位于 `if(QLIB_INSTALL)` 内。

| 规则 | 目标位置 |
|---|---|
| `install(TARGETS qlib EXPORT qlibTargets ...)` | 归档库与库文件到 `lib/`，运行库到 `bin/` |
| `install(DIRECTORY include/qlib ... FILES_MATCHING PATTERN "*.h")` | `include/qlib/` |
| `install(FILES <build>/include/qlib/qlib_export.h ...)` | `include/qlib/` |
| `install(EXPORT qlibTargets FILE qlibTargets.cmake NAMESPACE qlib:: ...)` | `lib/cmake/qlib/` |
| `write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)` | `lib/cmake/qlib/qlibConfigVersion.cmake` |
| `configure_package_config_file(cmake/qlibConfig.cmake.in ...)` | `lib/cmake/qlib/qlibConfig.cmake` |
| `configure_file(cmake/qlib.pc.in ...)` 然后 `install(FILES ...)` | `lib/pkgconfig/qlib.pc` |

默认静态构建下验证过的安装目录：

```text
include/qlib/ql_common.h
include/qlib/qlib_export.h
include/qlib/qlib.h
include/qlib/ql_mem.h
include/qlib/ql_vector.h
lib/cmake/qlib/qlibConfig.cmake
lib/cmake/qlib/qlibConfigVersion.cmake
lib/cmake/qlib/qlibTargets.cmake
lib/cmake/qlib/qlibTargets-noconfig.cmake
lib/libqlib.a
lib/pkgconfig/qlib.pc
```

pkg-config 文件生成时使用可重定位的前缀，而不是配置时的 `CMAKE_INSTALL_PREFIX`：

```text
prefix=${pcfiledir}/../..
```

`cmake --install --prefix <other>` 和 `DESTDIR` 暂存都会改变文件的写入位置，却不会更新配置阶段烘焙进去的值，因此写死的前缀会指向包从未安装到的目录。`${pcfiledir}` 则由 pkg-config 根据文件自身的位置解析。

## uninstall 目标

`cmake/cmake_uninstall.cmake.in` 被配置进构建目录，并由一个自定义目标驱动：

```bash
cmake --build build --target uninstall
```

它从构建目录读取 `install_manifest.txt`，清单缺失时以 `FATAL_ERROR` 失败，并删除其中记录的每一个路径。它只删除由同一构建目录的 `cmake --install` 所记录的文件。

## Sanitizer

`QLIB_SANITIZE` 使用目录作用域，而不是目标接口：

```cmake
if(QLIB_SANITIZE)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
    message(STATUS "qlib: sanitizers enabled (QLIB_SANITIZE=ON)")
endif()
```

这样安排位置和机制有两个原因：

- 该代码块位于 `add_library(qlib ...)` **之前**。目录属性会被其后创建的目标捕获，并由其后添加的子目录继承，因此 `qlib`、`qlib_test`、`qlib_harness_selftest` 和 `fuzz_vector` 都会得到这些选项，无需逐个指名。
- 它**不是** `INTERFACE` 目标。这些选项必须到达测试，而只链接到 `qlib` 的接口目标做不到这一点；同时它们不能成为已安装目标接口的一部分，而 `PUBLIC` 链接会导致这种结果。`add_link_options` 还覆盖链接步骤，而 sanitizer 需要这一步。

## 新增模块

下面的步骤就是 `ql_vector` 当初所做的，也是下一个模块的清单。把 `foo` 替换为模块名。

1. **编写公开头文件** `include/qlib/ql_foo.h`。从一个全新的包含守卫开始，包含 `<stddef.h>` 和 `"qlib/qlib_export.h"`（如有需要再加上 `"qlib/ql_common.h"` 和 `"qlib/ql_mem.h"`），并把所有内容包进 `extern "C"` 守卫。给每个导出函数标注 `QLIB_EXPORT`，并在忽略结果会导致 bug 的地方加上 `QLIB_NODISCARD`（[CONTRIBUTING.md](../../../CONTRIBUTING.md) 规则 9 和 10）。
2. **编写实现** `src/ql_foo.c`。在此定义任何不透明的 `struct ql_foo`。如果模块需要报告错误，就包含 `"ql_internal.h"`，并调用 `ql_err_raise()` 而不是直接操作错误处理器（规则 11）。所有分配都要经过调用方的 `ql_allocator_t`（规则 7）。
3. **注册源文件**到 [CMakeLists.txt](../../../CMakeLists.txt) 的 `QLIB_SRC` 列表中。
4. **把头文件加入伞形头文件**：在 [`qlib.h`](../../../include/qlib/qlib.h) 中加一行 `#include "qlib/ql_foo.h"`。不需要修改安装规则——`install(DIRECTORY include/qlib ... FILES_MATCHING PATTERN "*.h")` 会自动把它纳入。
5. **编写测试套件** `tests/test_foo.c`。先包含 `"test_suites.h"`，再包含被测模块的头文件。文件以 `MU_TEST_SUITE(test_suite_foo) { ... }` 结尾。
6. **声明测试套件**于 [`tests/test_suites.h`](../../../tests/test_suites.h)：添加 `MU_DECLARE_SUITE(test_suite_foo);`。这正是让 `-Wmissing-prototypes` 保持安静、并保证声明与定义一致的做法。
7. **运行测试套件**于 [`tests/test_main.c`](../../../tests/test_main.c)：在 `MU_REPORT()` 之前添加 `MU_RUN_SUITE(test_suite_foo);`。
8. **加入测试源文件**到 [`tests/CMakeLists.txt`](../../../tests/CMakeLists.txt) 中的 `add_executable(qlib_test ...)` 列表。
9. **添加模糊测试目标**，前提是该模块会解析或转换输入：`fuzz/fuzz_foo.c`，其中包含 `int LLVMFuzzerTestOneInput(const uint8_t*, size_t)` 并做前置声明，以便 `-Wmissing-prototypes` 保持安静，再在 [`fuzz/CMakeLists.txt`](../../../fuzz/CMakeLists.txt) 中加一个 `add_executable` 块和一个可选的 `add_test` 冒烟运行。
10. **提交前验证**：

```bash
cmake -S . -B build -DQLIB_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure

clang-format -i src/*.c include/qlib/*.h tests/*.c tests/*.h
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy src/*.c tests/*.c -p build
```

同样的步骤由 [`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml) 中的 `format`、`tidy`、`linux`、`macos`、`windows`、`sanitizers`、`fuzz`、`docs` 和 `package` job 强制执行；每个 job 守卫什么，见[测试](testing.md)。
