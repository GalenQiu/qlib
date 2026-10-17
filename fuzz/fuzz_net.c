#include "qlib/ql_net.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * libFuzzer entry point for the network module.
 *
 * The address parser is the one place in qlib that turns untrusted text into a
 * value, so it is the one that most deserves a fuzzer. The property worth
 * checking is the round trip: anything that parses must format back to text
 * that parses to an equal address, and formatting must be idempotent.
 *
 * A refusal is not a failure. The point is that a refusal is a clean refusal:
 * a defined error code, no wild pointer, and no write past the buffers, which
 * ASAN is watching.
 */

/* Longer than any legal address, so the parser sees plenty of malformed input. */
#define FUZZ_TEXT_MAX 64

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    char text[FUZZ_TEXT_MAX + 1];
    char out[QL_NET_ADDR_STR_MAX];
    char again_text[QL_NET_ADDR_STR_MAX];
    ql_net_addr_t addr;
    ql_net_addr_t again;
    size_t n;
    size_t length;

    n = size < FUZZ_TEXT_MAX ? size : FUZZ_TEXT_MAX;
    memcpy(text, data, n);
    text[n] = '\0';

    /* An embedded NUL simply truncates; the parser is handed a C string. */
    if (ql_net_addr_parse(text, &addr) != QL_OK)
    {
        return 0;
    }

    if (addr.family != QL_NET_AF_INET)
    {
        abort(); /* a successful parse is always IPv4 */
    }

    if (ql_net_addr_format(&addr, out, sizeof out) != QL_OK)
    {
        abort(); /* what parsed must also format */
    }

    length = strlen(out);
    if (length > 15U)
    {
        abort(); /* "255.255.255.255" is the longest IPv4 text */
    }

    /* The formatted text must parse back to the same address. */
    memset(&again, 0, sizeof again);
    if (ql_net_addr_parse(out, &again) != QL_OK)
    {
        abort();
    }
    if (!ql_net_addr_equal(&addr, &again))
    {
        abort();
    }

    /* And formatting that must reproduce the same text. */
    if (ql_net_addr_format(&again, again_text, sizeof again_text) != QL_OK)
    {
        abort();
    }
    if (strcmp(out, again_text) != 0)
    {
        abort();
    }

    /* One byte short of what the address needs must be refused. */
    if (ql_net_addr_format(&addr, again_text, length) != QL_ERR_OUT_OF_RANGE)
    {
        abort();
    }

    /*
     * The documented minimum is 16 bytes for IPv4 whatever the address needs,
     * not `strlen + 1`. Asserting the tighter rule here was a bug in this
     * harness: the library is entitled to reject a short buffer, and does.
     */
    if (ql_net_addr_format(&addr, again_text, 16) != QL_OK)
    {
        abort();
    }
    if (strcmp(out, again_text) != 0)
    {
        abort();
    }
    if (ql_net_addr_format(&addr, again_text, 15) != QL_ERR_OUT_OF_RANGE)
    {
        abort();
    }

    return 0;
}
