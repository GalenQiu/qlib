# qlib 中文文档

本目录包含两套文档，以及构建 API 参考所需的工具。

| 路径 | 读者 | 内容 |
|---|---|---|
| [`user/`](user/getting-started.md) | 库的使用者 | 构建、集成、各模块用法 |
| [`technical/`](technical/architecture.md) | 贡献者与维护者 | 架构、设计决策、ABI 策略、可移植性、构建系统、测试策略 |
| [`mainpage.md`](../mainpage.md) | Doxygen | 生成的 API 参考的着陆页；只有英文一版 |
| [`Doxyfile.in`](../Doxyfile.in) | Doxygen | 配置模板，由 CMake 填充 |

## 语言

英文文档位于 [`../`](../index.md)，本目录是它的简体中文版本：两套文档逐文件对应，
并且都会进入生成的 Doxygen 参考。唯一的例外是 `mainpage.md`：Doxygen 只能有一份
着陆页，因此它只有英文版。这条例外记录在 `ci/check_i18n.py` 的清单里，而不是让它
悄悄通过检查。

修改某一页时，请在同一次改动中一并更新对应的另一语言版本，或者明确说明该译文已过期。

两套文档树之间的相对链接由 CI 检查：

```bash
python3 ci/check_links.py
```

## 用户文档

1. [快速上手](user/getting-started.md) —— 构建库并运行测试
2. [集成到你的项目](user/integration.md) —— 通过 CMake、pkg-config 或直接引入源码
3. [错误处理](user/errors.md) —— 返回码、`ql_strerror`、错误回调
4. [内存管理](user/memory.md) —— 分配器抽象与分配辅助函数
5. [动态数组](user/vector.md) —— 可增长数组 `ql_vector_t`
6. [网络地址](user/net.md) —— 解析与格式化 IPv4 地址
7. [字符串](user/string.md) —— 可增长、始终以 NUL 结尾的字节串
8. [哈希表](user/hash.md) —— 定长键到定长值
9. [双向链表](user/list.md) —— 稳定的节点句柄与常数时间删除
10. [线程](user/thread.md) —— 线程、互斥量与条件变量
11. [日志](user/log.md) —— 级别、格式，以及控制台与轮转文件输出目标（sink）

## 技术文档

1. [架构](technical/architecture.md) —— 模块划分与分层
2. [设计决策](technical/design-decisions.md) —— 代码为什么是现在这个样子
3. [ABI 与版本](technical/abi-and-versioning.md) —— 什么可以改、什么时候能改
4. [可移植性](technical/portability.md) —— 支持的平台与相关的条件编译
5. [构建系统](technical/build-system.md) —— CMake 结构与如何新增模块
6. [测试](technical/testing.md) —— 测试、模糊测试与静态分析的策略
7. [开发工作流](development-workflow.md) —— 日常循环、新增模块清单，以及这个项目已经踩过的坑

## 构建 API 参考

API 参考由 [Doxygen](https://www.doxygen.nl/) 生成。CMake 通过 `QLIB_BUILD_DOCS`
选项驱动它：

```bash
cmake -S . -B build -DQLIB_BUILD_DOCS=ON
cmake --build build --target docs
```

生成结果位于 `docs/html/index.html`，用浏览器打开即可。生成的 `docs/html/` 与
`docs/latex/` 目录不纳入 git 跟踪。

如果更想手工运行 Doxygen，请自行填写 `Doxyfile.in` 里的占位符，再把 `doxygen`
指向生成出来的文件：

```bash
cd docs
sed -e 's|@PROJECT_SOURCE_DIR@|..|g' \
    -e 's|@PROJECT_BINARY_DIR@|..|g' \
    -e 's|@PROJECT_VERSION@|0.1.0|g' \
    -e 's|@PROJECT_NAME@|qlib|g' \
    Doxyfile.in > Doxyfile
doxygen Doxyfile
```

## 阅读约定

- 代码片段除非明确标注为"片段"，否则均可直接编译。
- 凡是 `const ql_allocator_t*` 形参处传 `NULL`，一律表示"使用默认分配器"。
- 所有返回 `ql_ret_t` 的函数，成功时返回 `QL_OK`（值为 0），因此正确的判断写法是
  `if (rc != QL_OK)`。
- 标识符、文件名、命令行、编译选项、CMake 选项名等一律保留英文原文，不做翻译。
  这一条只适用于中文文档，因此英文版没有对应的条目。

## 为什么这个文件叫 `index.md`

Doxygen 以**文件名**作为 Markdown 页面的标识。如果仓库根目录有一个 `README.md`，
而本目录也有一个，两者会落到同一个标识上：只有根目录那个会被发布，本页会从生成的
参考中悄无声息地消失，指向它的链接也会变成无法解析的 `\ref`——而文档 job 会把它
当作错误。

改名为 `index.md` 后它就有了自己的页面。代价是代码托管平台在浏览本目录时不会再
自动渲染它，这也是为什么生成的参考里由 [`mainpage.md`](../mainpage.md) 充当入口，
而仓库根目录的 `README.md` 指向本页。
