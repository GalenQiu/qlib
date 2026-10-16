# 测试

## 策略

测试套件有四层，每一层回答一个不同的问题：

| 层次 | 它回答的问题 | 位置 |
|---|---|---|
| 单元测试 | 文档化的行为是否成立，包括每一条错误路径？ | `tests/test_*.c` |
| 测试框架自检 | 失败的断言是否真的会让这次运行失败？ | `tests/selftest/` |
| 模糊测试 | 任意操作序列是否会破坏内存安全或不变量？ | `fuzz/fuzz_vector.c` |
| 变异测试 | 如果实现是错的，测试会不会察觉？ | 手动，见下文 |

前三层在每次 push 时由 CI 运行。第四层是一种评审技巧，而不是自动化的门禁——它的用途是找出因错误原因而通过的测试。

当前覆盖 39 个测试用例：`test_common.c` 中 5 个，`test_mem.c` 中 12 个，`test_vector.c` 中 22 个。它们都在同一个二进制 `qlib_test` 中运行，并以 `qlib_common_test` 注册到 ctest。

## minunit 测试框架

[`tests/minunit.h`](../../../tests/minunit.h) 用五个宏提供了整个测试框架：

| 宏 | 用途 |
|---|---|
| `MU_TEST(name)` | 把测试用例声明为 `static void name(void)` |
| `MU_TEST_SUITE(name)` | 声明一个调用 `MU_RUN_TEST` 的套件函数 |
| `MU_DECLARE_SUITE(name)` | 生成 `void name(void);`，使运行器与定义方对链接属性达成一致 |
| `MU_RUN_TEST(test)` | 先递增 `mu_tests_run`，再调用测试 |
| `MU_CHECK(cond)` | 记录一次失败并继续执行 |
| `MU_ASSERT(cond)` | 记录一次失败并从测试返回 |
| `MU_REPORT()` / `MU_EXIT_CODE` | 打印统计结果；若有任何失败则以非零状态退出 |

`MU_RUN_TEST` 在调用测试*之前*递增计数器，因此即使测试崩溃，也仍会被计为已运行。

### 这个测试框架最初的 bug

计数器原先定义在头文件中：

```c
/* 错误——每个翻译单元各有一份私有副本 */
static int mu_tests_run = 0;
static int mu_failures = 0;
```

头文件中的 `static` 定义会让**每个** `.c` 文件都拥有自己的一份副本。`MU_CHECK` 在 `test_common.c` 内展开，所以一次失败的断言递增的是*那个*文件的 `mu_failures`。`MU_REPORT` 和 `MU_EXIT_CODE` 位于 `test_main.c`，读取的是*它*的副本，而那份始终为零。可观察到的结果是：

```text
FAIL: tests/test_common.c:33: 1 == 2      <- 断言确实失败了
0 tests run, 0 failures                   <- 报告显示的却相反
exit status 0                             <- 进程还成功退出了
```

一个红色的测试看起来是绿的，因此 CI 永远不可能捕获这类回归。

修复办法是单一定义加外部声明：

```c
/* minunit.h */
extern int mu_tests_run;
extern int mu_failures;

/* minunit.c */
int mu_tests_run = 0;
int mu_failures = 0;
```

[`tests/minunit.c`](../../../tests/minunit.c) 的存在没有别的原因。两个 `.c` 文件都列在 `tests/CMakeLists.txt` 中，因此计数器真正跨翻译单元边界共享。

### `MU_ASSERT` 与 `MU_CHECK` 并用

`MU_CHECK` 在失败后继续执行。对于一串相互独立的比较，这正是你想要的；但只要下一条语句会解引用刚刚检查过的东西，它就是错的：

```c
MU_CHECK(state.message != NULL);
MU_CHECK(strlen(state.message) > 0);   /* 如果上面的检查失败，这里会崩溃 */
```

`MU_ASSERT` 记录失败并返回，因此测试会干净地中止，其余用例仍会运行。本测试套件通篇采用的经验法则是：

- 对已经可以安全比较的值用 `MU_CHECK`。
- 对之后会解引用其目标的指针，或用作除数、索引的值，用 `MU_ASSERT`。

这并非假想：变异测试（见下文）正是在 `tests/test_mem.c` 中暴露了这个模式，当时被变异的实现让测试段错误，而不是报告一次干净的失败。现在那里写的是 `MU_ASSERT(state.message != NULL);`。

### 新增测试套件

1. 创建 `tests/test_foo.c`，先包含 `"test_suites.h"`，并以一个 `MU_TEST_SUITE(test_suite_foo) { ... }` 块结束文件。
2. 向 [`tests/test_suites.h`](../../../tests/test_suites.h) 添加 `MU_DECLARE_SUITE(test_suite_foo);`。
3. 在 `MU_REPORT()` 之前向 [`tests/test_main.c`](../../../tests/test_main.c) 添加 `MU_RUN_SUITE(test_suite_foo);`。
4. 把 `test_foo.c` 加入 [`tests/CMakeLists.txt`](../../../tests/CMakeLists.txt) 中的 `add_executable(qlib_test ...)` 列表。

第 2 步不是可有可无的登记工作。`MU_TEST_SUITE` 定义的是具有外部链接的函数，而 `-Wmissing-prototypes`（属于本项目的警告集合）会拒绝没有可见原型的外部定义。`MU_DECLARE_SUITE` 同时被定义该套件的文件和运行器包含，因此原型与定义不可能不一致——而当 `test_main.c` 针对一个非 static 的定义手写 `static void test_suite_common(void);` 时，出错的正是这一点。

## 测试框架自检

`qlib_harness_selftest` 是一个可执行文件，其中包含一个**本应失败**的测试（[`tests/selftest/harness_suite.c`](../../../tests/selftest/harness_suite.c)）：

```c
MU_TEST(test_intentional_failure)
{
    int actual = 1;
    int expected = 2;

    MU_CHECK(actual == expected);
}
```

它与运行器（[`harness_main.c`](../../../tests/selftest/harness_main.c)）分处不同的翻译单元，正是为了让计数器必须跨 TU 边界——当初的 bug 就是因为缺少这一性质。比较通过变量进行，这样在 `-Werror` 下不会触发任何编译器的 `-Wtautological-compare`。

该测试套件通过一个驱动脚本而不是直接命令注册到 ctest：

```cmake
add_test(NAME qlib_harness_selftest
    COMMAND ${CMAKE_COMMAND}
        "-DPROBE=$<TARGET_FILE:qlib_harness_selftest>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/selftest/expect_failure.cmake"
)
```

[`expect_failure.cmake`](../../../tests/selftest/expect_failure.cmake) 运行探针并断言两件事：

1. 退出状态非零，且
2. 合并后的 stdout/stderr 恰好包含 `1 tests run, 1 failures`。

### 为什么否决了 `WILL_FAIL`

ctest 有一个内置的 `WILL_FAIL` 属性，可以反转测试结果，它显然是最先想到的选择。但它会对**任何**非零退出做反转，其中包括段错误、abort 或 sanitizer 失败。因此在到达其断言之前就崩溃的探针会被记为通过——这道守卫就会悄无声息地不再守卫。同时检查报告的计数和退出状态，意味着探针必须真正带着已记录的失败到达 `MU_REPORT`。

驱动脚本三条路径上验证过的行为：

| 探针 | 结果 |
|---|---|
| 一个退出码为 0 的二进制 | `FATAL_ERROR: ... a failing assertion exited with success.` |
| `/bin/false`（退出码 1，无报告） | `FATAL_ERROR: ... expected '1 tests run, 1 failures' in the report.` |
| 真实的 `qlib_harness_selftest` | `test harness self check passed (exit=1, report matched)` |

`CONTRIBUTING.md` 指出，这个套件不得被“修好”。

## `tests/.clang-tidy` 覆盖文件

[`.clang-tidy`](../../../.clang-tidy) 适用于整个仓库。[`tests/.clang-tidy`](../../../tests/.clang-tidy) 使用 `InheritParentConfig: true` 并禁用两项检查：

| 检查 | 原因 |
|---|---|
| `cert-err33-c` | `MU_CHECK`/`MU_ASSERT` 展开出的 `fprintf()` 结果被刻意忽略，所以每一条断言都会触发它。它对 `src/` 保持启用，因为那里未检查的返回值是真实缺陷。 |
| `readability-function-cognitive-complexity` | 这是针对生产代码逻辑的度量。一个测试函数本来就是长长的线性断言序列。 |

继承而不是复制，可以把已启用检查的列表集中在一处。注意根配置还禁用了 `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling` 和 `readability-implicit-bool-conversion`；这些排除同样适用于测试，根文件中有解释。

## 模糊测试

[`fuzz/fuzz_vector.c`](../../../fuzz/fuzz_vector.c) 是一个 libFuzzer 目标，仅在 `QLIB_BUILD_FUZZ=ON` 且编译器为 Clang（`-fsanitize=fuzzer`）时构建。换用其他编译器时，[`fuzz/CMakeLists.txt`](../../../fuzz/CMakeLists.txt) 会发出 `WARNING`，而不是悄无声息地什么都不产出。

### 输入字节的含义

输入是一段 vector 操作脚本，每个操作一个字节：

| 字节 | 含义 |
|---|---|
| `data[0]` | 元素大小：`(data[0] % 32) + 1`，即大小 1..32 |
| `data[i]`，其中 `i >= 1` | `op = data[i] % 8`，`index = data[i] % 64` |

| `op` | 操作 |
|---|---|
| 0 | `ql_vector_push_back(vec, elem)` |
| 1 | `ql_vector_pop_back(vec, NULL)` |
| 2 | `ql_vector_insert(vec, index, elem)` |
| 3 | `ql_vector_remove(vec, index)` |
| 4 | `ql_vector_reserve(vec, index)` |
| 5 | `ql_vector_resize(vec, index)` |
| 6 | `ql_vector_push_back(vec, ql_vector_at(vec, ...))` — 刻意的自别名 |
| 7 | `ql_vector_assign(vec, at, ql_vector_at(vec, at))` — 重叠的自赋值 |

操作 6 和 7 的存在专门是为了驱动[设计决策](design-decisions.md)中 ADR-9 的别名路径。索引由 `FUZZ_MAX_INDEX` 上限截到 64，好让 fuzzer 把时间花在状态转换上，而不是花在多 GB 级分配上。

元素载荷是从输入填充的 `uint8_t elem[32]` 缓冲区。它之所以是 32 字节数组而不是标量，原因就在其大小：更早的版本用的是 `uint32_t`，而 `elem_size` 可以取 32，于是 `ql_vector_insert` 读越了一个 4 字节栈对象的末尾。ASAN 在 5000 次运行内就把它标记为 `stack-buffer-overflow`。那是测试框架的 bug，不是库的 bug，源码中有说明修正的注释。

### 每次操作后检查的不变量

`check_invariants()` 在每个字节之后运行，一旦违反就 `abort()`，因此 libFuzzer 会把该输入记录为崩溃：

- `ql_vector_size(vec) <= ql_vector_capacity(vec)`。
- 每个有效槽位都可寻址：对所有 `i < size` 都有 `ql_vector_at(vec, i) != NULL`。
- 末尾之后的一个位置不可寻址：`ql_vector_at(vec, size) == NULL`。

### 结果

在此检出上全新运行一次：

```text
Done 6044768 runs in 46 second(s)
stat::average_exec_per_sec:     131408
```

没有发现。注意该目标是用 `-fsanitize=fuzzer,address,undefined` 编译的，因此这些运行同时覆盖 ASAN 和 UBSAN。

当 `QLIB_BUILD_FUZZ=ON` 时，ctest 还会注册 `fuzz_vector_smoke`（`-runs=2000 -max_len=128`），好让普通的 `ctest` 运行也会执行该目标。

## 变异测试

方法：破坏实现中的一项保证，重新构建，确认测试套件变红。存活下来的变异意味着对应的行为未被测试。

针对当前代码，在 ASAN+UBSAN 的 Debug 构建（`-DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug`）下运行：

| # | 变异 | 结果 |
|---|---|---|
| A | `ql_size_mul_overflow()`：把守卫替换为 `if (0)` | 退出码 1；ASAN 在 `ql_def_malloc` 中报告 `requested allocation size 0xfffffffffffffffe ... exceeds maximum supported size` |
| B | `ql_calloc()`：删除 `memset(ptr, 0, total)` | 退出码 1；`39 tests run, 64 failures` |
| C | `ql_alloc()`：删除 `QL_ERR_NOMEM` 的抛出 | 退出码 1；`39 tests run, 3 failures` |
| D | `ql_vector_push_back`/`insert`：绕过 `ql_vector_pin_elem()` | 退出码 1；ASAN 报告 `heap-use-after-free` |

有两点观察值得记录：

- 变异 A 是作为*内存安全*失败被捕获的，而不仅仅表现为断言失败。没有溢出检查时，回绕后的 `SIZE_MAX * 2` 会到达 `malloc`，ASAN 会拒绝它。因此即便某个构建没有针对该具体情况的测试，守卫的价值也依然可见。
- 变异 C 最初让测试套件**段错误**，而不是报告失败，因为 `test_failure_raises_nomem` 在失败的 `MU_CHECK(state.message != NULL)` 之后接着执行了 `strlen(state.message)`。这正是 `MU_ASSERT` 存在的具体原因，也是变异测试值得做的原因：否则该测试会被报告为崩溃，而不是一次干净的失败。

变异改动是机械地施加并回退的，之后已验证工作区逐字节一致。

第五个“变异”是永久的而不是手动的：测试框架自检是一个本应失败的测试，因此重新引入静态计数器 bug 会让探针以 0 退出，并让 ctest 驱动脚本大声失败。

## Sanitizer

`QLIB_SANITIZE=ON` 以目录作用域添加 `-fsanitize=address,undefined -fno-omit-frame-pointer -g`（GCC/Clang）或 `/fsanitize=address`（MSVC，它没有 UBSan），因此库、测试、测试框架探针和模糊测试目标都会被插桩。在 Linux 上，ASAN 包含 LeakSanitizer，无需额外配置即可捕获泄漏的块。

```bash
cmake -S . -B build -DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## CI 任务

定义在 [`.github/workflows/ci.yml`](../../../.github/workflows/ci.yml)。每个构建 job 都会传 `-DQLIB_WERROR=ON`，并全局设置 `CTEST_OUTPUT_ON_FAILURE=1`，以便失败的测试打印其输出。

| 任务 | 运行器 | 配置 | 它守卫什么 |
|---|---|---|---|
| `linux` | `ubuntu-24.04`，矩阵 `gcc` / `clang` | `-DQLIB_WERROR=ON` | 两个 Linux 编译器都无警告地构建并通过测试套件 |
| `macos` | `macos-latest` | `-DQLIB_WERROR=ON` | AppleClang 构建并通过——第三个声称支持的平台 |
| `windows` | `windows-latest`，矩阵 `shared` 取 `OFF`/`ON` | `-DQLIB_WERROR=ON -DQLIB_SHARED=...` | MSVC 构建；只有 `ON` 这一变体才真正走通 `QLIB_EXPORT`、`__declspec(dllexport)` 和 `QLIB_STATIC_DEFINE` |
| `sanitizers` | `ubuntu-24.04`，clang | `-DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` | 单元测试套件中没有内存错误或未定义行为 |
| `fuzz` | `ubuntu-24.04`，clang | `-DQLIB_BUILD_FUZZ=ON`，随后 `-max_total_time=120 -max_len=256` | 任意操作序列下的内存安全与不变量；失败时把 `crash-*` 作为 `fuzz-crash` 工件上传 |
| `package` | `ubuntu-24.04` | 安装到某个前缀，然后构建两个使用方 | `find_package(qlib 0.1 REQUIRED)` 和 `pkg-config` 都能解析并链接 |
| `docs` | `ubuntu-24.04` | `python3 ci/check_links.py`，随后 `-DQLIB_BUILD_DOCS=ON` 和 `--target docs` | Markdown 中的每个相对链接都能解析，并且 `Doxyfile.in` 仍能解析、仍能生成 `docs/html/index.html`；参考文档作为 `api-reference` 工件上传 |
| `format` | `ubuntu-24.04` | `pip install clang-format==23.1.1` | 对 `git ls-files '*.c' '*.h'` 得到的每个文件做格式检查，版本被固定，因为不同发行版输出不同 |
| `tidy` | `ubuntu-24.04` | 来自 LLVM apt 仓库的 clang-tidy 23 | 通过 `-p build` 对 `src/*.c` 和 `tests/*.c` 做静态分析；`WarningsAsErrors: '*'` 使任何发现都是致命的 |

有两个缺口值得明确说明。`tidy` job 只分析 `src/` 和 `tests/`，因此 `fuzz/fuzz_vector.c` 不在 clang-tidy 的覆盖范围内——除非 `QLIB_BUILD_FUZZ=ON`，它不在编译数据库中。另外，`format` job 的 `git ls-files` 只覆盖已跟踪的文件，因此新的源文件必须先加入索引，CI 才会检查它。
