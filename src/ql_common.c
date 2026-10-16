#include "ql_internal.h"

#include <errno.h>

static ql_err_cb ql_error_handler = NULL;
static void* ql_error_handler_userdata = NULL;

const char* ql_version(void)
{
    return QLIB_VERSION_STRING;
}

const char* ql_strerror(ql_ret_t code)
{
    switch (code)
    {
    case QL_OK:
        return "success";
    case QL_ERR_NOMEM:
        return "out of memory";
    case QL_ERR_INVALID_ARG:
        return "invalid argument";
    case QL_ERR_OUT_OF_RANGE:
        return "value out of range";
    case QL_ERR_EMPTY:
        return "container is empty";
    case QL_ERR_NOT_FOUND:
        return "not found";
    case QL_ERR_IO:
        return "I/O error";
    case QL_ERR_SYS:
        return "system error";
    case QL_ERR_AGAIN:
        return "temporarily unavailable";
    case QL_ERR_TIMEOUT:
        return "timed out";
    case QL_ERR_BUSY:
        return "resource busy";
    case QL_ERR_EXISTS:
        return "already exists";
    case QL_ERR_NOT_SUPPORTED:
        return "not supported";
    case QL_ERR_OVERFLOW:
        return "arithmetic overflow";
    }
    /* Deliberately no default above, so -Wswitch flags a missed new code. */
    return "unknown error";
}

ql_ret_t ql_ret_from_errno(int err)
{
    /*
     * Every case is guarded because the POSIX errno set is much wider than the
     * MSVC one. The aliases EWOULDBLOCK and EOPNOTSUPP are deliberately not
     * used: they share a value with EAGAIN and ENOTSUP on glibc, which would
     * make the switch fail to compile with duplicate cases.
     */
    switch (err)
    {
    case 0:
        return QL_OK;
    case ENOMEM:
        return QL_ERR_NOMEM;
    case EINVAL:
        return QL_ERR_INVALID_ARG;
    case ERANGE:
        return QL_ERR_OUT_OF_RANGE;
    case ENOENT:
        return QL_ERR_NOT_FOUND;
    case EIO:
        return QL_ERR_IO;
#ifdef EEXIST
    case EEXIST:
        return QL_ERR_EXISTS;
#endif
#ifdef EAGAIN
    case EAGAIN:
        return QL_ERR_AGAIN;
#endif
#ifdef ETIMEDOUT
    case ETIMEDOUT:
        return QL_ERR_TIMEOUT;
#endif
#ifdef EBUSY
    case EBUSY:
        return QL_ERR_BUSY;
#endif
#ifdef EOVERFLOW
    case EOVERFLOW:
        return QL_ERR_OVERFLOW;
#endif
#ifdef ENOTSUP
    case ENOTSUP:
        return QL_ERR_NOT_SUPPORTED;
#endif
    default:
        return QL_ERR_SYS;
    }
}

void ql_set_error_handler(ql_err_cb cb, void* userdata)
{
    ql_error_handler = cb;
    ql_error_handler_userdata = userdata;
}

void ql_get_error_handler(ql_err_cb* cb, void** userdata)
{
    if (cb != NULL)
    {
        *cb = ql_error_handler;
    }
    if (userdata != NULL)
    {
        *userdata = ql_error_handler_userdata;
    }
}

void ql_err_raise(ql_ret_t code, const char* message)
{
    if (ql_error_handler != NULL)
    {
        ql_error_handler(code, message != NULL ? message : ql_strerror(code), ql_error_handler_userdata);
    }
}
