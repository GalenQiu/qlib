# 集成

## 作为 CMake 子目录

把源码作为副本引入并使用 `add_subdirectory()` 是最简单的做法，不需要任何安装步骤：

```cmake
add_subdirectory(third_party/qlib)
target_link_libraries(myapp PRIVATE qlib::qlib)
```

始终链接带命名空间的 `qlib::qlib` 目标，而不要用裸的 `qlib`。如果以后改用
`find_package()` 来消费 qlib，正是这个别名让目标仍然可用，同时它还携带了包含
目录和 C11 要求。

走这条路时不会构建 qlib 自身的测试：只要 qlib 不是顶层项目，
`QLIB_BUILD_TESTS` 就默认为 `OFF`。如果确实需要这些测试，请显式设置
`-DQLIB_BUILD_TESTS=ON`。

`qlib::qlib` 会把 C 标准作为使用要求传递出去。因此，用纯 C89/C99 编写的使用方
仍能正确编译 qlib 的头文件，因为语言级别是目标的属性，而不是你的项目的属性。

## 从已安装的包使用

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build
cmake --install build
```

安装会把头文件放到 `<prefix>/include/qlib/`，库放到 `<prefix>/lib/`，CMake 包
配置放到 `<prefix>/lib/cmake/qlib/`。使用方随后这样写：

```cmake
find_package(qlib 0.2 REQUIRED)
target_link_libraries(myapp PRIVATE qlib::qlib)
```

用 `-DCMAKE_PREFIX_PATH=/your/prefix` 让 CMake 找到该前缀，也可以直接设置
`qlib_DIR=/your/prefix/lib/cmake/qlib`。

生成的 `qlibConfigVersion.cmake` 采用 `SameMajorVersion` 兼容策略，因此
`find_package(qlib 0.2)` 会被任何不低于该版本的已安装 `0.x` 满足——包括更晚的 `0.x`
发行版——并被 `1.0` 拒绝。当主版本号是 `0` 时，次版本号**不是** ABI 边界：见
[ABI 与版本](../technical/abi-and-versioning.md)。版本信息在构建期还可
通过 `QLIB_VERSION_MAJOR`、`QLIB_VERSION_MINOR`、`QLIB_VERSION_PATCH` 和
`QLIB_VERSION_STRING` 获得，在运行期可以通过 `ql_version()` 获得。

`cmake --install` 支持暂存前缀（staging prefix），这正是打包者需要的：

```bash
DESTDIR=/tmp/stage cmake --install build
```

也支持卸载：

```bash
cmake --build build --target uninstall
```

它会精确删除最近一次安装记录在 `install_manifest.txt` 中的那些文件。

## 配合 pkg-config

安装后的 qlib 还会附带一个 `qlib.pc`，因此非 CMake 构建可以照常工作：

```bash
cc myapp.c $(pkg-config --cflags --libs qlib) -o myapp
```

`.pc` 文件是可重定位的：它根据文件自身的位置推导前缀，因此用
`cmake --install --prefix` 或 `DESTDIR` 暂存该包时它依然正确。

## 不使用任何构建系统

把 `include/` 以及 `src/` 下的 `.c` 文件复制到你的代码树中并编译它们。有两点需要
注意：

- `include/qlib/ql_common.h` 会包含 `qlib/qlib_export.h`。那个头文件由 CMake 生成
  到 `<build>/include/qlib/`，因此要么在 `-Iinclude` 之外再加上
  `-I<build>/include`，要么在不需符号导出修饰的平台上自己提供一个空的替代头文件。
- 在 Windows 上，**静态**库的使用方必须定义 `QLIB_STATIC_DEFINE`。否则
  `QLIB_EXPORT` 会展开为 `__declspec(dllimport)`，导致构建失败。DLL 的使用方
  不应定义它。

## C++ 使用方

头文件用 `extern "C"` 包裹，可以安全地从 C++ 中包含：

```cpp
#include <qlib/qlib.h>

ql_vector_t* v = nullptr;
if (ql_vector_create(nullptr, sizeof(int), &v) == QL_OK) {
    // ...
    ql_vector_destroy(v);
}
```

以 C++17 或更新标准编译时，`QLIB_NODISCARD` 会映射为 `[[nodiscard]]`，因此在
C++ 中忽略返回值同样会产生警告。

## 安装的内容

```
<prefix>/include/qlib/ql_common.h
<prefix>/include/qlib/ql_mem.h
<prefix>/include/qlib/ql_vector.h
<prefix>/include/qlib/qlib.h
<prefix>/include/qlib/qlib_export.h      # 由 CMake 生成
<prefix>/lib/libqlib.a                   # 或 libqlib.so / qlib.dll
<prefix>/lib/cmake/qlib/qlibConfig.cmake
<prefix>/lib/cmake/qlib/qlibConfigVersion.cmake
<prefix>/lib/cmake/qlib/qlibTargets.cmake
<prefix>/lib/pkgconfig/qlib.pc
```

`src/ql_internal.h` 被有意排除在安装之外：它是 qlib 自身各翻译单元之间的私有接口。
