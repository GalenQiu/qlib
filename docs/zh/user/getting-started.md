# 快速上手

## 环境要求

- 支持 C11 的编译器：GCC、Clang、AppleClang 或 MSVC
- CMake 3.16 或更高版本

构建本库及其测试不需要其他任何东西。只有在想生成 API 参考时才需要
Doxygen，只有在想构建模糊测试目标时才需要 Clang。

## 构建与测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

测试二进制也可以直接运行，这在反复迭代时很方便：

```bash
./build/bin/qlib_test
```

它只打印一行汇总信息，任何失败都会让退出码非零：

```
39 tests run, 0 failures
```

在 Windows 上使用多配置生成器（Visual Studio、Xcode）时，二进制文件会落在
每个配置对应的子目录中，因此每条构建和测试命令都必须指定配置：

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\bin\Debug\qlib_test.exe
```

## 构建选项

每个选项都是普通的 CMake 缓存变量，在配置阶段用 `-D` 传入即可。

| 选项 | 默认值 | 作用 |
|---|---|---|
| `QLIB_BUILD_TESTS` | 顶层项目为 `ON`，其他情况为 `OFF` | 构建单元测试 |
| `QLIB_BUILD_FUZZ` | `OFF` | 构建 libFuzzer 目标。需要 Clang；其他编译器只会给出警告 |
| `QLIB_BUILD_BENCH` | `OFF` | 尚未实现；启用后会打印一条警告 |
| `QLIB_BUILD_NET` | `OFF` | 尚未实现；启用后会打印一条警告 |
| `QLIB_SHARED` | `OFF` | 构建共享库而不是静态库 |
| `QLIB_WERROR` | `OFF` | 把编译器警告视为错误。建议在 CI 中启用 |
| `QLIB_INSTALL` | `ON` | 生成安装与包配置规则 |
| `QLIB_SANITIZE` | `OFF` | 使用 AddressSanitizer 和 UndefinedBehaviorSanitizer 构建 |
| `QLIB_BUILD_DOCS` | `OFF` | 用 Doxygen 生成 API 参考 |

只有当 qlib 是顶层项目时，`QLIB_BUILD_TESTS` 才默认为 `ON`，因此用
`add_subdirectory()` 引入 qlib 的项目只会拿到库，不会多出一套测试。

## 使用 sanitizer 构建

当怀疑出现内存缺陷时，这是第一个应该尝试的办法，也是本地运行测试的默认方式：

```bash
cmake -S . -B build-asan -DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

`QLIB_SANITIZE=ON` 会添加 `-fsanitize=address,undefined -fno-omit-frame-pointer
-g`。在 MSVC 下只能得到 AddressSanitizer，因为 MSVC 没有
UndefinedBehaviorSanitizer。

## 模糊测试

```bash
CC=clang cmake -S . -B build-fuzz -DQLIB_BUILD_FUZZ=ON
cmake --build build-fuzz
./build/bin/fuzz_vector -max_total_time=60
```

`-fsanitize=fuzzer` 是 Clang 的特性。使用 GCC 或 MSVC 时该选项只会给出警告，
不会生成任何模糊测试目标。

## 生成 API 参考

```bash
cmake -S . -B build -DQLIB_BUILD_DOCS=ON
cmake --build build --target docs
```

之后打开 `docs/html/index.html`。手动运行 Doxygen 的方式见[文档索引](../index.md)。

## 第一个程序

```c
#include <qlib/qlib.h>
#include <stdio.h>

int main(void)
{
    ql_vector_t* vec = NULL;
    int i;

    /* NULL 表示"使用默认分配器"。 */
    if (ql_vector_create(NULL, sizeof(int), &vec) != QL_OK) {
        return 1;
    }

    for (i = 0; i < 10; i++) {
        if (ql_vector_push_back(vec, &i) != QL_OK) {
            ql_vector_destroy(vec);
            return 1;
        }
    }

    printf("%zu elements, capacity %zu\n", ql_vector_size(vec), ql_vector_capacity(vec));

    ql_vector_destroy(vec);
    return 0;
}
```

`<qlib/qlib.h>` 是一个伞形头文件，会引入所有模块。也可以包含各个单独的头文件，
例如 `<qlib/ql_vector.h>`，同样没问题，而且编译更快。

## 下一步

- [集成](integration.md) —— 如何在你自己的构建中依赖 qlib
- [错误处理](errors.md) —— 如何处理返回码
- [内存管理](memory.md) —— 分配器与分配辅助函数
- [向量](vector.md) —— 使用可增长数组
