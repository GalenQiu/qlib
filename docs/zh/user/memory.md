# 内存管理

qlib 从不替你调用 `malloc`。每次分配都要经过你提供的 `ql_allocator_t`，传入
`NULL` 表示"使用默认的、由 libc 支撑的分配器"。

## 分配器

```c
typedef struct {
    ql_malloc_fn  malloc;
    ql_realloc_fn realloc;
    ql_free_fn    free;
    void*         userdata;
    void*         reserved[4];
} ql_allocator_t;
```

三个回调的最后一个参数都是 `userdata`，自定义分配器正是通过它访问自己的状态：

```c
typedef struct {
    size_t live_blocks;
    size_t peak_bytes;
} my_state;

static void* my_malloc(size_t size, void* userdata)
{
    my_state* state = userdata;
    void* p = malloc(size);
    if (p != NULL) {
        state->live_blocks++;
    }
    return p;
}

static void* my_realloc(void* ptr, size_t size, void* userdata)
{
    (void)userdata;
    return realloc(ptr, size);
}

static void my_free(void* ptr, void* userdata)
{
    my_state* state = userdata;
    if (ptr != NULL) {
        state->live_blocks--;
    }
    free(ptr);
}

my_state state = {0, 0};
ql_allocator_t alloc = {
    .malloc = my_malloc,
    .realloc = my_realloc,
    .free = my_free,
    .userdata = &state,
};
```

需要遵守的规则：

- **三个函数指针都必须非 NULL。** 如果只想覆盖部分行为，请传
  `ql_default_allocator()`。
- `reserved` 必须清零。像上面那样使用指定初始化器就会自动清零。这些槽位的存在是
  为了让该结构体能在不破坏 ABI 的前提下扩展；请不要使用它们。
- 回调必须遵循其同名 libc 函数的 C 语义。qlib 不要求 `realloc(ptr, 0)` 有某种
  特定行为，因为 qlib 从不发起这种调用。

`ql_default_allocator()` 返回一个完整填充、由 libc 支撑的分配器：

```c
ql_allocator_t alloc = ql_default_allocator();
```

## 分配器是复制的，不是引用的

`ql_vector_create()` 会把 `ql_allocator_t` 复制进向量。调用者的结构体不必比容器
活得更久：

```c
ql_vector_t* make_vector(void)
{
    ql_allocator_t alloc = ql_default_allocator();   /* 栈上局部变量，没问题 */
    ql_vector_t* vec = NULL;
    if (ql_vector_create(&alloc, sizeof(int), &vec) != QL_OK) {
        return NULL;
    }
    return vec;    /* vec 拥有自己的 `alloc` 副本 */
}
```

`userdata` 同样是按值复制的，因此如果它指向某个东西，那个东西在容器被使用时必须
仍然存活。

## 分配辅助函数

`<qlib/ql_mem.h>` 提供了这些基础构件。它们都接受 `NULL` 作为分配器，表示使用默认
分配器。

| 函数 | 说明 |
|---|---|
| `ql_alloc(alloc, size)` | `size == 0` 时返回 `NULL`，且不算错误 |
| `ql_calloc(alloc, count, size)` | 先检查溢出，然后清零 |
| `ql_realloc(alloc, ptr, size)` | `size == 0` 时释放 `ptr` 并返回 `NULL` |
| `ql_alloc_array(alloc, count, size)` | `count * size`，检查溢出 |
| `ql_realloc_array(alloc, ptr, count, size)` | 同上，用于调整大小 |
| `ql_aligned_alloc(alloc, alignment, size)` | 超额对齐的块；用 `ql_aligned_free` 释放 |
| `ql_aligned_free(alloc, ptr)` | 释放由 `ql_aligned_alloc` 得到的块；`NULL` 是空操作 |
| `ql_dealloc(alloc, ptr)` | `NULL` 是空操作 |
| `ql_size_mul_overflow(count, size, &result)` | 单独的检查函数 |
| `ql_size_add_overflow(a, b, &result)` | 单独的加法检查 |

```c
#include <qlib/qlib.h>

int* values = ql_alloc_array(NULL, 16, sizeof(int));
if (values == NULL) {
    /* 要么内存不足，要么 16 * sizeof(int) 溢出了 */
}

/* 扩容会保留原有内容；失败时原指针不受影响。 */
int* bigger = ql_realloc_array(NULL, values, 32, sizeof(int));
if (bigger == NULL) {
    ql_dealloc(NULL, values);   /* values 仍然有效，并且仍然归你所有 */
} else {
    values = bigger;
    ql_dealloc(NULL, values);
}
```

这些函数都标记了 `QLIB_NODISCARD`，因此丢弃返回值会产生编译器警告。忽略分配结果
几乎总是缺陷。

## 溢出检查

`count * size` 是经典的整数溢出错误。`ql_alloc_array()` 和 `ql_realloc_array()`
不会犯这个错：它们会用 `QL_ERR_OVERFLOW` 拒绝请求，而不是算出一个回绕后的大小、
再分配一块过小的缓冲区。

```c
size_t total = 0;
if (ql_size_mul_overflow(count, elem_size, &total) != QL_OK) {
    /* count * elem_size 无法用 size_t 表示 */
}
```

如果 `result` 是 `NULL`，`ql_size_mul_overflow()` 返回 `QL_ERR_INVALID_ARG`；
乘积溢出时返回 `QL_ERR_OVERFLOW`。失败时 `result` 保持不变。`count` 为 0 永远
不会溢出。

`ql_size_add_overflow()` 是同样的检查，用于加法；在计算带填充或带帧头的大小时很
有用：

```c
size_t total = 0;
if (ql_size_add_overflow(payload, header, &total) != QL_OK) {
    /* payload + header 无法用 size_t 表示 */
}
```

请使用这些辅助函数，而不要自己计算算术；这是项目的贡献规则之一。

## 对齐分配

`ql_aligned_alloc()` 返回按调用方选定的 2 的幂对齐的内存，适用于 SIMD 缓冲区、
按缓存行分隔、DMA 或按页对齐的 I/O。

```c
#include <qlib/qlib.h>

/* 按 64 字节对齐，共 4096 字节；用 ql_aligned_free 释放，而不是 ql_dealloc。 */
void* buf = ql_aligned_alloc(NULL, 64, 4096);
if (buf == NULL) {
    /* 对齐字节数无效、溢出，或内存不足 */
}
/* ... 使用 buf ... */
ql_aligned_free(NULL, buf);
```

| 参数 | 规则 |
|---|---|
| `alignment` | 非零的 2 的幂。其他任何值都会得到 `QL_ERR_INVALID_ARG` |
| `size` | 字节数。与 C11 的 `aligned_alloc` 不同，它**不**要求是对齐字节数的整数倍 |

需要遵守的规则：

- **用 `ql_aligned_free()` 释放。** 你拿到的指针并不是分配器返回的那个指针，因此
  对它使用 `ql_dealloc()`、`ql_realloc()` 或 libc `free()` 都是错的。
- 向 `ql_aligned_free()` 传入与分配时相同的分配器。
- 没有对齐版的 realloc。要扩容，就分配一个新块、复制，然后释放旧块。

该块是从分配器自己的 `malloc` 中划出来的，而不是来自平台的对齐分配器。
`ql_aligned_alloc()` 申请 `size + alignment + sizeof(void*)` 字节，把地址向上取整，
并把原始指针存放在返回地址紧前面的那些字节里。因此自定义分配器始终掌控着自己的
内存，代价就是那笔每次分配的开销。较大的对齐字节数因此代价按比例更高：1 MiB 的
对齐字节数下，无论 `size` 是多少，每次分配都会浪费约 1 MiB。

因为返回的指针是 `alignment` 的整数倍，所以对于任何对齐字节数不超过它的类型，它
也是适当对齐的。

## 失败上报

分配失败时，辅助函数返回 `NULL`，**并且**通过[错误回调](errors.md)上报
`QL_ERR_NOMEM`。数组请求溢出时上报 `QL_ERR_OVERFLOW`。你要根据返回值分支；
回调的存在是为了让某个中心位置能观察到失败。

## 所有权小结

| 对象 | 由谁释放 |
|---|---|
| 来自 `ql_alloc`/`ql_calloc`/`ql_alloc_array`/`ql_realloc*` 的内存 | 你，用 `ql_dealloc`，并使用同一个分配器 |
| 来自 `ql_aligned_alloc` 的内存 | 你，用 `ql_aligned_free`，并使用同一个分配器 |
| 来自 `ql_vector_create` 的容器 | 你，用 `ql_vector_destroy`，它会释放缓冲区和控制块 |
| 来自 `ql_vector_at`/`ql_vector_data` 的指针 | 不需要任何人；它们指向容器的缓冲区，并会因修改操作而失效 |
| 你传入的分配器结构体 | 你；qlib 会复制它，从不保存你的指针 |

## 线程安全

除了错误回调之外，分配器和这些辅助函数不持有全局状态，因此只要每个容器只在一个
线程中使用，它们就可以安全地用于多个线程。容器内部没有同步机制，也没有锁模块：
qlib 中还没有任何并发的东西，因此也没有任何东西宣称自己是并发安全的。
