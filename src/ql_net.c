#include "qlib/ql_net.h"

#include "ql_internal.h"

#include <stdio.h>
#include <string.h>

/* "255.255.255.255" plus the terminator. */
#define QL_NET_IPV4_STR_SIZE 16

static bool ql_net_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

ql_ret_t ql_net_addr_parse(const char* text, ql_net_addr_t* out)
{
    ql_net_addr_t addr;
    const char* p;
    size_t index;

    if (text == NULL || out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: text or out is NULL");
        return QL_ERR_INVALID_ARG;
    }

    /*
     * A colon means the caller wrote IPv6. Saying "not supported" is more
     * useful than the syntax error the octet parser would otherwise report.
     */
    if (strchr(text, ':') != NULL)
    {
        ql_err_raise(QL_ERR_NOT_SUPPORTED, "ql_net_addr_parse: IPv6 is not supported yet");
        return QL_ERR_NOT_SUPPORTED;
    }

    memset(&addr, 0, sizeof addr);
    p = text;

    for (index = 0; index < 4; index++)
    {
        unsigned value = 0;
        int digits = 0;

        if (index > 0)
        {
            if (*p != '.')
            {
                ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: expected '.' between octets");
                return QL_ERR_INVALID_ARG;
            }
            p++;
        }

        while (ql_net_is_digit(*p))
        {
            /*
             * Reject "010" rather than pick a base for it. inet_pton() on glibc
             * also rejects it, and silently choosing octal here would be the
             * kind of surprise that turns into a security bug elsewhere.
             */
            if (digits == 0 && *p == '0' && ql_net_is_digit(p[1]))
            {
                ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: octet has a leading zero");
                return QL_ERR_INVALID_ARG;
            }

            value = (value * 10U) + (unsigned)(*p - '0');
            if (value > 255U)
            {
                ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_net_addr_parse: octet is above 255");
                return QL_ERR_OUT_OF_RANGE;
            }

            digits++;
            p++;
        }

        if (digits == 0)
        {
            ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: octet is empty");
            return QL_ERR_INVALID_ARG;
        }

        addr.bytes[index] = (unsigned char)value;
    }

    /* Exactly four octets, and nothing left over. */
    if (*p != '\0')
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_parse: trailing characters after the address");
        return QL_ERR_INVALID_ARG;
    }

    addr.family = QL_NET_AF_INET;
    *out = addr;
    return QL_OK;
}

ql_ret_t ql_net_addr_format(const ql_net_addr_t* addr, char* buf, size_t size)
{
    int written;

    if (addr == NULL || buf == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_format: addr or buf is NULL");
        return QL_ERR_INVALID_ARG;
    }

    if (addr->family == QL_NET_AF_INET6)
    {
        ql_err_raise(QL_ERR_NOT_SUPPORTED, "ql_net_addr_format: IPv6 is not supported yet");
        return QL_ERR_NOT_SUPPORTED;
    }
    if (addr->family != QL_NET_AF_INET)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_net_addr_format: not an IPv4 address");
        return QL_ERR_INVALID_ARG;
    }

    if (size < QL_NET_IPV4_STR_SIZE)
    {
        ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_net_addr_format: buffer is too small");
        return QL_ERR_OUT_OF_RANGE;
    }

    written = snprintf(buf, size, "%u.%u.%u.%u", (unsigned)addr->bytes[0], (unsigned)addr->bytes[1],
                       (unsigned)addr->bytes[2], (unsigned)addr->bytes[3]);
    if (written < 0 || (size_t)written >= size)
    {
        ql_err_raise(QL_ERR_OUT_OF_RANGE, "ql_net_addr_format: buffer is too small");
        return QL_ERR_OUT_OF_RANGE;
    }

    return QL_OK;
}

bool ql_net_addr_equal(const ql_net_addr_t* a, const ql_net_addr_t* b)
{
    size_t length;

    if (a == NULL || b == NULL || a->family != b->family)
    {
        return false;
    }

    /* Compare only the bytes the family actually uses, so padding is ignored. */
    length = a->family == QL_NET_AF_INET ? 4U : sizeof a->bytes;
    return memcmp(a->bytes, b->bytes, length) == 0;
}
