#ifndef QLIB_QL_COMMON_H
#define QLIB_QL_COMMON_H

#include <stddef.h>

#include "qlib/qlib_export.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 库返回错误码
     */
    typedef enum
    {
        QL_OK = 0,
        QL_ERR_NOMEM,
        QL_ERR_INVALID_ARG,
        QL_ERR_OUT_OF_RANGE,
        QL_ERR_EMPTY,
        QL_ERR_NOT_FOUND,
        QL_ERR_IO,
        QL_ERR_SYS,
    } ql_ret_t;

    /**
     * @brief 可选错误信息回调
     * @param msg 人类可读错误字符串
     * @param userdata 用户自定义上下文
     */
    typedef void (*ql_err_cb)(const char* msg, void* userdata);

    /**
     * @brief 内存分配器函数原型
     */
    typedef void* (*ql_malloc_fn)(size_t size, void* userdata);
    typedef void* (*ql_realloc_fn)(void* ptr, size_t size, void* userdata);
    typedef void (*ql_free_fn)(void* ptr, void* userdata);

    /**
     * @brief 内存分配器抽象结构
     */
    typedef struct
    {
        ql_malloc_fn malloc;
        ql_realloc_fn realloc;
        ql_free_fn free;
        void* userdata;
    } ql_allocator_t;

    /**
     * @brief 锁抽象层，用于线程安全扩展
     */
    typedef void (*ql_lock_fn)(void* userdata);
    typedef void (*ql_unlock_fn)(void* userdata);

    typedef struct
    {
        ql_lock_fn lock;
        ql_unlock_fn unlock;
        void* userdata;
    } ql_lock_t;

    /**
     * @brief 获取默认分配器（封装libc malloc/realloc/free）
     */
    QLIB_EXPORT ql_allocator_t ql_default_allocator(void);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_COMMON_H */
