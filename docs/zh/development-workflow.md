# 开发工作流

这份文档面向**后续维护和扩展 qlib 的人**，把"改代码 → 验证 → 提交 → 推送 → 看 CI"
这条链路写成可照做的步骤，并记录这个项目已经踩过的坑。

它是一份**操作手册**，不是设计文档。设计理由见
[设计决策](technical/design-decisions.md)，构建系统的细节见
[构建系统](technical/build-system.md)。

> 这份文档中英各有一份，`docs/development-workflow.md` 是英文版。两份一起维护，
> 都不是事后补的翻译。

---

## 1. 项目现状速览

| 项目 | 当前值 |
|---|---|
| 版本 | `0.2.0`（`SOVERSION 0`） |
| 语言标准 | **恰好 C11，禁用编译器扩展**（`-std=c11`，不是 `gnu11`） |
| 模块 | `ql_common`（错误/版本）、`ql_mem`（分配器）、`ql_str`（字符串）、`ql_hash`（哈希表）、`ql_list`（双向链表）、`ql_vector`（动态数组）、`ql_thread`（线程，可选）、`ql_net`（IPv4 与 TCP，可选） |
| 公开头文件 | `ql_common.h`、`ql_mem.h`、`ql_str.h`、`ql_hash.h`、`ql_list.h`、`ql_vector.h`、`ql_thread.h`、`ql_net.h`、`qlib.h`（伞形） |
| 单元测试 | 启用线程与日志 **147 个**，仅启用线程 **127 个**，默认 **118 个**，关闭网络模块 **95 个** |
| 模糊测试目标 | 6 个（`fuzz_str`、`fuzz_hash`、`fuzz_list`、`fuzz_vector`、`fuzz_mem`、`fuzz_net`） |
| 基准测试用例 | 16 个 |
| 本地验证 | `./ci/check.sh`，**12 个步骤** |
| CI | GitHub Actions，**9 个 job 定义 → 一次运行 11 个 job**（`linux` 和 `windows` 是矩阵） |
| 远端 | `origin` = Gitee（主），`github` = GitHub 镜像 |

CI 徽章：

```markdown
[![CI](https://github.com/GalenQiu/qlib/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/GalenQiu/qlib/actions/workflows/ci.yml)
```

---

## 2. 环境准备

必须：

- C11 编译器（GCC / Clang / AppleClang / MSVC）
- CMake ≥ 3.16
- Python 3（链接检查、中英一致性检查、CI 里的工具安装）
- git

强烈建议装上，否则本地验证会跳过对应步骤：

| 工具 | 用途 | 缺失后果 |
|---|---|---|
| `clang-format` | 格式门禁 | `format` 步骤 SKIP |
| `clang-tidy` | 静态分析 | `tidy` 步骤 SKIP |
| `doxygen` + `graphviz` | API 参考 | `docs` 步骤 SKIP |
| `clang` | 模糊测试（`-fsanitize=fuzzer`） | `fuzz` 步骤 SKIP |
| `pkg-config` | 消费方验证 | `package` 步骤只测 `find_package` |

`./ci/check.sh` 开头会打印所有工具的版本。**SKIP 不等于通过**——看到 SKIP 就说明
这一步没验证到。

---

## 3. 日常开发循环

### 3.1 确认起点是干净的

```bash
git status                 # 期望：干净
git pull origin main       # 如果 Gitee 上有别的改动
```

### 3.2 改代码

写代码时先过一遍 [CONTRIBUTING.md](../../CONTRIBUTING.md) 的规则。最常违反的三条：

- 公开函数必须标 `QLIB_EXPORT`
- 返回值不能忽略的地方要标 `QLIB_NODISCARD`
- 数组分配必须走 `ql_alloc_array` / `ql_realloc_array`，不要自己算乘积

### 3.3 本地验证（核心步骤）

```bash
./ci/check.sh --quick      # 快速：约 5 秒，跳过 sanitizer 和模糊测试
./ci/check.sh              # 完整：约 40 秒
```

输出形如：

```
step_links     PASS
step_i18n      PASS
step_format    PASS
step_win32     PASS
step_build     PASS
step_net_off   PASS
step_tidy      PASS
step_package   PASS
step_docs      PASS
step_sanitizers SKIP skipped by --quick
step_fuzz      SKIP skipped by --quick
9 passed, 0 failed, 2 skipped
```

失败时会直接打印该步骤的日志尾部，并在最后一行汇总：

```
failed: step_format
```

日志留在 `build-check/<步骤名>.log`。

**提交前至少跑一次 `--quick`。** 改动涉及分配、内存布局、解析或构建系统时，跑完整版。

### 3.4 提交

提交信息规范：`[模块] 简短描述`，例如：

```
[mem] implement ql_aligned_alloc / ql_aligned_free
[net] implement the optional network module: IPv4 address parsing
[ci] fix the two jobs the first real CI run failed
```

正文建议写**为什么**而不是改了什么（`git diff` 已经说明改了什么）。这个项目的提交
历史里记录了几个决策的来龙去脉，值得保持。

### 3.5 推送并看 CI

```bash
git push origin main       # Gitee
git push github main       # GitHub（CI 在这里跑）
```

**CI 只在 GitHub 上跑。** Gitee 用 Gitee Go，语法不同，本仓库没有配置。

等约 4~5 分钟，然后：

```bash
# 看最近一次运行
curl -sS "https://api.github.com/repos/GalenQiu/qlib/actions/runs?per_page=1" \
  | python3 -c "import json,sys;d=json.load(sys.stdin)['workflow_runs'][0];print(d['status'],d['conclusion'],d['html_url'])"

# 看每个 job
curl -sS "https://api.github.com/repos/GalenQiu/qlib/actions/runs/<run_id>/jobs" \
  | python3 -c "
import json,sys
for j in json.load(sys.stdin)['jobs']:
    print(f\"{j['name']:36} {j['conclusion']}\")
    for s in j['steps']:
        if s['conclusion']=='failure': print('   FAILED:', s['name'])
"
```

或直接打开 `https://github.com/GalenQiu/qlib/actions`。

> 未认证的 GitHub API **每小时只有 60 次请求**，很容易吃光——写这份文档时就吃了一
> 次。别写轮询间隔小于 20 秒的脚本；一个 run 轮询 40 次就没了。想要更多额度就配一个
> token（`Authorization: Bearer <token>`）。

**只想看"绿不绿"的话，用徽章，不要用 API。** 徽章走的是 `github.com` 而不是
`api.github.com`，不受配额限制：

```bash
curl -sSL "https://github.com/GalenQiu/qlib/actions/workflows/ci.yml/badge.svg?branch=main" \
  | grep -oE 'passing|failing'
```

### 3.6 CI 红了怎么办

1. **先在本地复现**：`./ci/check.sh`。它现在覆盖了 CI 的绝大部分，包括安装与消费方验证。
2. 打开失败 job 的日志，找到失败步骤。
3. 对照下表定位。

| 失败 job | 本地能复现吗 | 说明 |
|---|---|---|
| `linux / gcc`、`linux / clang` | ✅ `step_build` | 编译器版本与本地不同，可能是新警告 |
| `sanitizers` | ✅ `step_sanitizers` | |
| `fuzz` | ✅ `step_fuzz` | |
| `format` | ✅ `step_format` | CI 固定 clang-format 23.1.1，本地版本可能不同 |
| `tidy` | ✅ `step_tidy` | |
| `docs` | ✅ `step_docs` | CI 固定 Doxygen 1.18.0 |
| `install / find_package + pkg-config` | ✅ `step_package` | |
| `windows / msvc` | ❌ 只有 `step_win32` 的桩检查 | 真实 MSVC 只能靠 CI |
| `macos / clang` | ❌ | AppleClang + `NET=OFF` 配置 |

**如果本地全绿而 CI 红，先怀疑"本地验证的东西和 CI 验证的不是同一份"。** 这在这个
项目里真实发生过一次，见第 8.3 节。

---

## 4. 新增一个模块：完整清单

以新增 `ql_foo` 为例。这份清单是 `ql_vector` 和 `ql_net` 实际走过的路径。

### 4.1 写公开头文件

`include/qlib/ql_foo.h`：

- 开头必须有 `@file` 文档块。**没有它，Doxygen 不会为该头文件生成文档页**，
  里面的函数会出现在搜索索引里但没有任何页面展示（这个坑踩过，见 8.4）
- include guard、`#include "qlib/qlib_export.h"`、`extern "C"` 包裹
- 每个公开函数标 `QLIB_EXPORT`
- 该标 `QLIB_NODISCARD` 的（返回值不能忽略的）标上
- 容器类型用不透明类型；值语义结构体必须带尾部 `reserved[]`
- **每个结构体成员都要有文档注释**，否则 `docs` 步骤会因警告而失败

### 4.2 写实现

`src/ql_foo.c`：

- `#include "ql_internal.h"` 才能用 `ql_err_raise()`
- 所有内存走调用方的 `ql_allocator_t`，不要直接 `malloc`
- 错误路径要 `ql_err_raise()` + 返回错误码

### 4.3 接入构建

`CMakeLists.txt` 的 `QLIB_SRC` 列表加 `src/ql_foo.c`。

**如果模块是可选的**（像 `QLIB_BUILD_NET` 那样），
[构建系统](technical/build-system.md) 的「可选模块」一节列了必须联动的四处：源文件列表、
`PUBLIC` 编译定义、头文件安装规则、`qlib.pc` 的 `Cflags`。少一处就会出现
"头文件发出去但符号不存在"。

### 4.4 加进伞形头文件

`include/qlib/qlib.h` 加一行（可选模块用 `#ifdef` 包起来）。

### 4.5 写测试

- 新建 `tests/test_foo.c`，**第一个 include 是 `"test_suites.h"`**
- 文件末尾用 `MU_TEST_SUITE(test_suite_foo) { ... }`
- `tests/test_suites.h` 加 `MU_DECLARE_SUITE(test_suite_foo);`
- `tests/test_main.c` 加 `MU_RUN_SUITE(test_suite_foo);`
- `tests/CMakeLists.txt` 的源文件列表加 `test_foo.c`（可选模块要条件添加）

断言的选择：**后面紧跟着解引用指针的检查用 `MU_ASSERT`，其余用 `MU_CHECK`**。
`MU_CHECK` 失败后会继续执行，写成 `MU_CHECK(p != NULL); p[0] = 1;` 会段错误而不是
干净失败。

### 4.6 加模糊测试（如果模块解析或转换输入）

`fuzz/fuzz_foo.c`，`int LLVMFuzzerTestOneInput(const uint8_t*, size_t)`，
**要提前声明原型**（否则 `-Wmissing-prototypes` 报错），`fuzz/CMakeLists.txt`
的 `QLIB_FUZZ_TARGETS` 列表加目标名。

不变量要真的检查。`fuzz_net.c` 用往返不变量（能解析的必须能格式化回等价地址），
`fuzz_vector.c` 每步之后检查 `size <= capacity` 和槽位可寻址。

### 4.7 加基准测试（如果成本是使用者会关心的）

`bench/bench_foo.c` 定义 `ql_bench_suite_foo`，`bench/bench.h` 声明，
`bench/bench_main.c` 的 `g_suites[]` 注册，`bench/CMakeLists.txt` 加源文件。

用例要检查它测的那个结果（`QL_BENCH_CHECK`）——**给坏代码计时比不计时更糟**。
准备数据放在 `setup` 钩子里，否则构建输入的时间会算进被测操作。

### 4.8 写文档

新增 `docs/user/foo.md` 和 `docs/zh/user/foo.md`，并在两个 `index.md`、
`mainpage.md` 里登记。

### 4.9 验证

```bash
./ci/check.sh
```

---

## 5. 文档维护

### 5.1 中英必须同步

英文文档在 `docs/`，中文在 `docs/zh/`，**逐文件对应**。改了一边就必须改另一边。

`ci/check_i18n.py` 会检查两件事：

1. **文件集合**：每篇英文文档有中文对应（例外：`mainpage.md` 只能有一份）
2. **标识符**：**逐页**比较所有 `ql_*` / `QLIB_*` 名字。某页英文里提到的函数名，
   该页中文里也必须出现——**在别的页里出现不算**，因为那帮不到读这一页的人

翻译时：

- 标识符、路径、命令、编译选项、工具名**一律保留英文**
- 代码块原样复制，只有块内注释翻译
- 改了英文页要**在同一次提交**里更新中文页，否则在文档里注明译文已过期

内置的例外只剩一条，写在 `ci/check_i18n.py` 里并注明了理由：
`mainpage.md`（英文独有，因为 Doxygen 只接受一个着陆页）。

### 5.2 文档里的代码示例必须能编译

`docs/user/*.md` 里的示例会被读者直接复制。**我踩过这个坑**：`vector.md` 的示例
忽略了 `QLIB_NODISCARD` 的返回值，照抄下来会产生警告。

写示例时：

- 每个可能失败的调用都检查返回值
- 声称的输出要真的跑一遍确认（`net.md` 和 `vector.md` 都验证过逐字节一致）
- 只描述性出现的片段要明确标注为"片段"

### 5.3 生成参考

```bash
cmake -S . -B build -DQLIB_BUILD_DOCS=ON
cmake --build build --target docs      # → docs/html/index.html
```

CI 里这一步**出现任何警告即失败**（固定 Doxygen 1.18.0）。

---

## 6. 版本发布

版本号在两个地方：

| 位置 | 作用 |
|---|---|
| `CMakeLists.txt` 的 `project(qlib VERSION 0.2.0)` | 决定 `SOVERSION` 与 `qlibConfigVersion.cmake` |
| `include/qlib/ql_common.h` 的 `QLIB_VERSION_*` | 编进头文件，供运行期比较 |

**两处必须一起改**，否则 `ql_version()` 与头文件宏不一致，消费者做版本检查会误判。

发布检查单：

1. 两处版本号同步更新
2. `./ci/check.sh` 全绿
3. 更新 `ql_ret_t`？（**只允许在末尾追加**，绝不能重排）
4. 是否动了 `ql_allocator_t` 的布局？（动了就是主版本变更）
5. 是否动了任何公开函数的签名、语义或所有权约定？（同上）
6. 打 tag 并推送到两个远端：

```bash
git tag -a v0.2.0 -m "qlib 0.2.0"
git push origin v0.2.0 && git push github v0.2.0
```

ABI 承诺的完整清单见 [ABI 与版本](technical/abi-and-versioning.md)。**0.x 期间严格来说
任何版本都可能破坏兼容**，`SameMajorVersion` 检查拦不住，所以上面的检查单不是形式。

---

## 7. 日常命令速查

```bash
# 本地验证
./ci/check.sh                     # 全部 11 步
./ci/check.sh --quick             # 跳过 sanitizer 与模糊测试
./ci/check.sh --fuzz-seconds=60   # 加大模糊测试预算

# 单独跑某一步
python3 ci/check_links.py         # 相对链接
python3 ci/check_i18n.py          # 中英一致性
clang-format -i src/*.c include/qlib/*.h tests/*.c tests/*.h fuzz/*.c bench/*.c bench/*.h

# 单独构建
cmake -S . -B build -DQLIB_WERROR=ON -DQLIB_BUILD_BENCH=ON
cmake --build build && ctest --test-dir build --output-on-failure
./build/bin/qlib_bench            # 基准测试
./build/bin/qlib_bench --quick

# 各种配置
cmake -S . -B build -DQLIB_BUILD_NET=OFF          # 关掉网络模块
cmake -S . -B build -DQLIB_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
CC=clang cmake -S . -B build -DQLIB_BUILD_FUZZ=ON

# 推送
git push origin main && git push github main
```

---

## 8. 踩过的坑

这一节是这个项目真实付过代价的地方。**新加代码前值得扫一眼。**

### 8.1 测试框架：`static` 计数器跨翻译单元

**症状**：断言失败了，测试程序却打印 `0 tests run, 0 failures` 并返回 0，CI 永远绿。

**原因**：`minunit.h` 里把计数器写成 `static int mu_failures = 0;`。头文件里的
`static` 让**每个 `.c` 各拿到一份副本**——`test_common.c` 里递增的是它自己那份，
`test_main.c` 里读的是另一份全零的。

**现状**：计数器改为 `extern`，唯一定义在 `minunit.c`。并且有
`tests/selftest/` 里的"注定失败探针" + ctest 驱动守着，回归会让 ctest 变红。

### 8.2 测试可能没有牙齿

**做法**：写完测试后，**故意破坏实现**，确认测试会红。这叫变异测试。

这个项目记录在案的变异：去掉溢出判断、去掉 `calloc` 的 `memset`、去掉 `NOMEM` 上报、
绕过别名保护、去掉对齐让位、去掉 2 的幂校验、头部存错指针——每一个都被抓住了。

**这项实践本身也抓到过测试的 bug**：某个测试先 `MU_CHECK(state.message != NULL)`
再 `strlen(state.message)`，断言失败时**段错误**而不是干净失败。这就是 `MU_ASSERT`
存在的理由。

### 8.3 两份测试夹具 = 一份太多

**症状**：CI 的 `package` job 红了，本地 `./ci/check.sh` 全绿。

**原因**：workflow **内联生成**了一个 consumer 程序，而我本地复现时手写了另一份。
当 `ql_allocator_t` 从 `ql_common.h` 搬到 `ql_mem.h` 时，workflow 里那份编译不过了，
而本地那份还在正常工作——**分叉因此静默存在了好几个提交**。

**现状**：consumer 签入为 `ci/consumer/`，workflow 和 `ci/check.sh` 构建**同一份文件**。

**教训**：**本地复现必须使用 CI 的同一份输入、同一条命令。** 否则你验证的是"我写的
等价物能跑"，不是"CI 里那份能跑"。

### 8.4 Doxygen 用文件名作 Markdown 页面标识

**症状**：某个文档页在生成的参考里完全不存在，指向它的链接变成 `\ref` 无法解析的警告。

**原因**：Doxygen 以**文件名**作为页面标识。仓库根目录和 `docs/` 各有一个
`README.md`，两者塌缩到同一个标识上，只有根目录那个被发布。

**现状**：`docs/index.md` 和 `docs/zh/index.md`（不是 README.md）。代价是代码托管
平台浏览目录时不再自动渲染它。

### 8.5 没有 `@file` 就没有文档页

**症状**：Doxygen 报 0 警告，看起来一切正常，但**生成的参考里一个 API 函数都没有**。

**原因**：`EXTRACT_ALL = NO` 时，头文件必须有 `@file` 文档块才会生成文档页。当时只有
`qlib.h` 有，其余三个头文件里的函数被索引了却无处展示——**"无页面"就没有警告**。

**教训**：**"0 警告"不等于"产出了正确的东西"。** 补上 `@file` 后立刻又暴露出 4 个
未文档化的宏和一个缺失的 `@param`。

### 8.6 `configure_file` 会替换注释里的 `@NAME@`

**症状**：生成的配置文件里注释被改坏了。

**原因**：CMake 的 `configure_file(@ONLY)` 替换**每一个** `@名字@`，包括注释里的。
未定义的名字替换成空串，于是注释被悄悄吃掉。

**做法**：模板文件的注释里**不要写 at 号包裹的记号**。

### 8.7 `grep` 一个不存在的文件 = 假通过

**症状**：门禁"通过"了，其实什么都没检查。

**原因**：`grep` 在文件不存在时返回非零。`if grep -q "warning:" missing.log; then
失败; fi` 于是走进 else 分支，打印"没有警告"。

**这个错误我犯了两次**——一次是 grep 一个 cmake target 根本不写的日志文件（于是
"0 警告"是对一个不存在的文件测出来的），一次是脚本里 `grep` 不到匹配就继续。

**做法**：读文件前先 `test -f`；`grep` 的返回值要么显式处理，要么用 `if ... then` 而
不是靠它决定"没问题"。

### 8.8 `target_compile_features` 只是下限

**症状**：CONTRIBUTING 写着"C11，无编译器扩展"，实际却按 C23 编译，扩展全开。

**原因**：`target_compile_features(c_std_11)` 表达的是**下限**。编译器默认方言已经
满足时，CMake **一个 `-std=` 标志都不加**。GCC 16 默认 C23，于是代码按 C23 编。

**现状**：`CMAKE_C_STANDARD` + `C_STANDARD_REQUIRED` + `C_EXTENSIONS OFF` 三者一起，
才真正emit `-std=c11`。

**教训**：**要验证构建标志真的生效，去看 `compile_commands.json`**，不要假设。

### 8.9 模糊测试可能先抓到脚手架自己的 bug

`fuzz_mem` 第一版跑 57000 次就报 `out-of-memory`。排查结果是**我的脚手架错了**：
"无效对齐"分支直接传原始 32 位值，其中一个恰好是 `0x80000000`——**它是货真价实的
2 的幂**，库正确地接受了它，并按文档化的约定尝试分配一个对齐大小的内存块（2 GiB）。

**教训**：模糊测试报告问题时，**先分清是库的缺陷还是脚手架的缺陷**，别急着改库。

### 8.10 `.clang-format` 与代码风格冲突

`BreakBeforeBraces: Attach` 和全仓库的 Allman 风格冲突：照 CONTRIBUTING 跑一次
`clang-format -i` 会改掉每个函数签名。另外两个相关坑：

- `BraceWrapping` 的逐项覆盖**只在 `BreakBeforeBraces: Custom` 下生效**，写别的值是
  死配置
- `extern "C"` 的花括号换行后，不设 `IndentExternBlock: NoIndent` 会**把整个头文件
  主体缩进一级**

### 8.11 ASCII-armored 的 apt 签名密钥需要 dearmor

**症状**：CI 里 `apt-get update` 报仓库未签名，工具装不上。

**原因**：把 armored 密钥直接管进 `signed-by` keyring。apt 的校验器（gpgv）**不会
自动 dearmor**，于是读不出来。

**做法**：`curl ... | sudo gpg --dearmor -o /usr/share/keyrings/xxx.gpg`。
本地可以用 `gpgv --keyring` 验证：armored 报 `invalid packet`，dearmored 报
`Good signature`。

### 8.12 `set -euo pipefail` 与"没找到"的 grep

`set -euo pipefail` 下，`grep -E "error|warning" | head -3` 在**没有匹配**时整体
返回非零，`set -e` 于是中止脚本——**看起来像"构建失败"，其实是构建成功**（没有错误
可匹配）。

**做法**：这类命令后面加 `|| true`，或者别用 `set -e` 而显式处理返回值。

### 8.13 修改文件做实验后一定要恢复

用 `sed` / `python` 改源码做变异测试后，用 `git checkout -- <文件>` 恢复，
并确认 `git status` 干净。别依赖 `/tmp` 里的备份——某些沙箱环境里 `/tmp` 是
**每条命令独立的**，备份会消失。

---

## 9. 工具版本固定策略

CI 里三个工具固定了版本，因为它们的输出会随版本变化，而**"没有新警告"这种承诺只有
对着一个固定版本才有意义**：

| 工具 | CI 版本 | 来源 | 为什么固定 |
|---|---|---|---|
| clang-format | 23.1.1 | PyPI | 格式输出在版本间会变 |
| clang-tidy | 23.x | apt.llvm.org | 检查集与告警文本会变 |
| Doxygen | 1.18.0 | GitHub Release tarball | 警告输出会变 |

**本地不必装同版本**，但要知道：本地格式检查通过、CI 格式检查失败，通常就是版本差异。

`./ci/check.sh` 刻意**不**固定版本，用本机装了什么就用什么——它是便利工具，不是 CI 的
替代品。
