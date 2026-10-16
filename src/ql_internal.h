#ifndef QLIB_INTERNAL_H
#define QLIB_INTERNAL_H

#include "qlib/ql_common.h"

/*
 * Internal helpers shared between translation units.
 *
 * Deliberately not marked QLIB_EXPORT: these are not part of the public API and
 * stay private to the library on a shared Windows build.
 */

/**
 * @brief Report an error to the installed handler, if any.
 *
 * @param code    Error code to report.
 * @param message Static description; NULL falls back to ql_strerror(code).
 */
void ql_err_raise(ql_ret_t code, const char* message);

#endif /* QLIB_INTERNAL_H */
