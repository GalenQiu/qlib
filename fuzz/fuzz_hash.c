#include "qlib/ql_hash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * libFuzzer entry point for ql_hash.
 *
 * The likely bugs in an open-addressing table are in deletion and growth: a
 * tombstone that hides a key living further along its probe chain, a rehash
 * that loses an entry, or a growth check that lets the table fill up and makes
 * probing loop forever.
 *
 * So this drives the table from a shadow model. The input is a stream of byte
 * pairs -- operate on the key `pair >> 1`, insert when the low bit is set and
 * remove when it is clear -- and after every operation the table has to agree
 * with the model exactly. A table that never finds a key it holds, or keeps a
 * key it removed, fails immediately rather than silently.
 */

#define KEYS 128

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    ql_hash_t* hash = NULL;
    bool present[KEYS];
    size_t expected = 0;
    size_t i;
    int key;

    memset(present, 0, sizeof present);

    if (ql_hash_create(NULL, sizeof(int), sizeof(int), &hash) != QL_OK)
    {
        abort();
    }

    for (i = 0; i + 1 < size; i += 2)
    {
        bool insert = (data[i] & 1U) != 0;
        int k = data[i + 1] % KEYS;
        int value = k * 3;

        if (insert)
        {
            if (ql_hash_set(hash, &k, &value) != QL_OK)
            {
                abort();
            }
            if (!present[k])
            {
                present[k] = true;
                expected++;
            }
        }
        else
        {
            ql_ret_t rc = ql_hash_remove(hash, &k);

            if (present[k])
            {
                if (rc != QL_OK)
                {
                    abort();
                }
                present[k] = false;
                expected--;
            }
            else if (rc != QL_ERR_NOT_FOUND)
            {
                abort(); /* removing something absent has to say so */
            }
        }

        if (ql_hash_size(hash) != expected)
        {
            abort();
        }
    }

    /* Every key agrees with the model, present or not. */
    for (key = 0; key < KEYS; key++)
    {
        int out = -1;
        int k = key;
        ql_ret_t rc = ql_hash_get(hash, &k, &out);

        if (present[key])
        {
            if (rc != QL_OK || out != key * 3)
            {
                abort();
            }
            if (!ql_hash_contains(hash, &k))
            {
                abort();
            }
        }
        else
        {
            if (rc != QL_ERR_NOT_FOUND)
            {
                abort();
            }
            if (ql_hash_contains(hash, &k))
            {
                abort();
            }
        }
    }

    /* Iteration visits exactly the live entries, once each. */
    {
        ql_hash_iter_t it;
        unsigned seen[KEYS];
        size_t visited = 0;

        memset(seen, 0, sizeof seen);
        ql_hash_iter_init(&it, hash);
        for (;;)
        {
            const void* raw_key = NULL;
            const void* raw_value = NULL;
            int k;

            if (!ql_hash_iter_next(&it, &raw_key, &raw_value))
            {
                break;
            }
            if (raw_key == NULL || raw_value == NULL)
            {
                abort();
            }

            memcpy(&k, raw_key, sizeof k);
            if (k < 0 || k >= KEYS || !present[k] || seen[k] != 0U)
            {
                abort();
            }
            seen[k] = 1;
            visited++;
        }

        if (visited != expected)
        {
            abort();
        }
    }

    /* Clearing must return the table to a working empty state. */
    ql_hash_clear(hash);
    if (ql_hash_size(hash) != 0)
    {
        abort();
    }
    for (key = 0; key < KEYS; key++)
    {
        int k = key;

        if (ql_hash_contains(hash, &k))
        {
            abort();
        }
    }

    ql_hash_destroy(hash);
    return 0;
}
