#ifndef QLIB_QL_COMMON_H
#define QLIB_QL_COMMON_H

/**
 * @file ql_common.h
 * @brief Version macros, the ql_ret_t error codes, ql_strerror(), and the
 *        optional error handler.
 *
 * This module depends on nothing but the C library.
 */

#include <stddef.h>

#include "qlib/qlib_export.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------------------------------------------------------------------
 * Version
 * ------------------------------------------------------------------------ */

/** @brief Major component of the version of these headers. */
#define QLIB_VERSION_MAJOR 0
/** @brief Minor component of the version of these headers. */
#define QLIB_VERSION_MINOR 2
/** @brief Patch component of the version of these headers. */
#define QLIB_VERSION_PATCH 0
/** @brief Version string of these headers, e.g. "0.2.0". Compare with ql_version(). */
#define QLIB_VERSION_STRING "0.2.0"

/**
 * @brief Version of the library actually linked in, e.g. "0.2.0".
 *
 * Compare it against QLIB_VERSION_STRING to detect a header/library mismatch.
 * @return A static string; never NULL.
 */
QLIB_EXPORT const char* ql_version(void);

/* ---------------------------------------------------------------------------
 * Attribute helpers
 * ------------------------------------------------------------------------ */

/**
 * @brief Marks a return value that must not be silently discarded.
 *
 * Degrades to nothing on compilers that know no equivalent.
 */
#if defined(__cplusplus) && __cplusplus >= 201703L
#  define QLIB_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#  define QLIB_NODISCARD __attribute__((warn_unused_result))
#elif defined(_Check_return_)
#  define QLIB_NODISCARD _Check_return_
#else
#  define QLIB_NODISCARD
#endif

/* ---------------------------------------------------------------------------
 * Error codes
 * ------------------------------------------------------------------------ */

/**
 * @brief Library return codes.
 *
 * New codes are only ever appended. The numeric values are part of the ABI and
 * must not be reordered or renumbered.
 */
typedef enum
{
    QL_OK = 0,            /**< Success. */
    QL_ERR_NOMEM,         /**< Allocation failed. */
    QL_ERR_INVALID_ARG,   /**< NULL pointer or an argument outside its contract. */
    QL_ERR_OUT_OF_RANGE,  /**< Representable, but outside the valid range. */
    QL_ERR_EMPTY,         /**< The operation needs a non-empty container. */
    QL_ERR_NOT_FOUND,     /**< A lookup found nothing. */
    QL_ERR_IO,            /**< I/O failure. */
    QL_ERR_SYS,           /**< Unmapped platform or OS error. */
    QL_ERR_AGAIN,         /**< Temporarily unavailable; retrying may succeed. */
    QL_ERR_TIMEOUT,       /**< A deadline expired. */
    QL_ERR_BUSY,          /**< The resource is in use. */
    QL_ERR_EXISTS,        /**< The item is already present. */
    QL_ERR_NOT_SUPPORTED, /**< Not available on this platform or in this build. */
    QL_ERR_OVERFLOW,      /**< Arithmetic overflow; the request was rejected. */
} ql_ret_t;

/**
 * @brief Human readable description of a return code.
 * @return A static string; never NULL, even for an out-of-range value.
 */
QLIB_EXPORT const char* ql_strerror(ql_ret_t code);

/**
 * @brief Translate a platform errno value into a ql_ret_t.
 *
 * 0 maps to QL_OK; anything unrecognised maps to QL_ERR_SYS.
 */
QLIB_EXPORT ql_ret_t ql_ret_from_errno(int err);

/* ---------------------------------------------------------------------------
 * Error handler
 * ------------------------------------------------------------------------ */

/**
 * @brief Optional sink for errors raised inside the library.
 *
 * @param code     Code identifying the failure.
 * @param message  Static, human readable description; never NULL.
 * @param userdata Opaque pointer registered with ql_set_error_handler().
 *
 * @warning The handler runs on the thread that hit the error and must not call
 *          back into the library. Keep it cheap: log, count, or set a flag.
 */
typedef void (*ql_err_cb)(ql_ret_t code, const char* message, void* userdata);

/**
 * @brief Install the error handler, or remove it by passing NULL.
 *
 * @note Intended to be called during start up, before other threads read it.
 */
QLIB_EXPORT void ql_set_error_handler(ql_err_cb cb, void* userdata);

/**
 * @brief Retrieve the installed handler. Either output pointer may be NULL.
 */
QLIB_EXPORT void ql_get_error_handler(ql_err_cb* cb, void** userdata);

#ifdef __cplusplus
}
#endif

#endif /* QLIB_QL_COMMON_H */
