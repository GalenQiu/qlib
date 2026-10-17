#include "qlib/ql_hash.h"

#include "ql_internal.h"

#include <stdint.h>
#include <string.h>

/* Smallest slot array; a power of two, which the masking below relies on. */
#define QL_HASH_MIN_CAPACITY 8

/* "No slot", used while probing. A local constant rather than SIZE_MAX so the
 * value does not depend on which limit macros a platform's <stdint.h> exposes. */
#define QL_HASH_NO_SLOT ((size_t)-1)

/*
 * Slot tags.
 *
 *   0        empty
 *   1        tombstone: something was here and was removed
 *   >= 3     a live entry, holding (hash | 1) for a hash forced to at least 2
 *
 * Two values are therefore never mistaken for a live tag, and a live tag is
 * never 0, so a zeroed tag array is a valid empty table.
 *
 * Deletion writes a tombstone rather than shifting entries back. A tombstone
 * keeps probe chains intact -- a lookup for a key that lives past it still
 * finds it -- and insertion reuses the first one seen. The cost is that
 * tombstones occupy capacity, so the growth check counts them and rehashing
 * clears them.
 */
struct ql_hash
{
    ql_allocator_t alloc;
    size_t key_size;
    size_t value_size;
    size_t slot_capacity;
    size_t count;
    size_t tombstones;
    uint64_t* tags;
    unsigned char* keys;
    unsigned char* values;
};

/** @brief What a probe found. */
typedef struct
{
    /** @brief Slot holding the key, or the slot to insert into. */
    size_t slot;
    /** @brief True when @c slot already holds this key. */
    bool found;
} ql_hash_probe_result;

/** @brief FNV-1a, 64 bit. */
static uint64_t ql_hash_bytes(const void* data, size_t len)
{
    const unsigned char* bytes = data;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;

    for (i = 0; i < len; i++)
    {
        hash ^= (uint64_t)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/** @brief Turn a key into a live slot tag, keeping it at 3 or above. */
static uint64_t ql_hash_tag(const void* key, size_t key_size)
{
    uint64_t hash = ql_hash_bytes(key, key_size);

    /* 0 and 1 are reserved, so nudge the two hashes that could collide. */
    if (hash < UINT64_C(2))
    {
        hash += 3;
    }
    return hash | UINT64_C(1);
}

/**
 * @brief Walk the probe chain for @p key.
 *
 * Returns the matching slot if the key is present, otherwise the slot to insert
 * into: the first tombstone on the chain, or the empty slot that ends it.
 *
 * Terminates because growth keeps at least a quarter of the slots empty.
 */
static ql_hash_probe_result ql_hash_probe(const ql_hash_t* hash, const void* key, uint64_t tag)
{
    size_t mask = hash->slot_capacity - 1;
    size_t slot = (size_t)tag & mask;
    size_t first_tombstone = QL_HASH_NO_SLOT;
    ql_hash_probe_result result;

    for (;;)
    {
        uint64_t here = hash->tags[slot];

        if (here == 0)
        {
            result.slot = first_tombstone != QL_HASH_NO_SLOT ? first_tombstone : slot;
            result.found = false;
            return result;
        }
        if (here == 1)
        {
            if (first_tombstone == QL_HASH_NO_SLOT)
            {
                first_tombstone = slot;
            }
        }
        else if (here == tag && memcmp(hash->keys + (slot * hash->key_size), key, hash->key_size) == 0)
        {
            result.slot = slot;
            result.found = true;
            return result;
        }

        slot = (slot + 1) & mask;
    }
}

/**
 * @brief Resize the slot array, dropping tombstones and reinserting live entries.
 *
 * The new arrays are installed before reinserting so that ql_hash_probe() works
 * against them unchanged; the old ones are freed afterwards.
 */
static ql_ret_t ql_hash_rehash(ql_hash_t* hash, size_t new_capacity)
{
    size_t tag_bytes;
    size_t key_bytes;
    size_t value_bytes;
    size_t value_stride = hash->value_size != 0 ? hash->value_size : 1;
    uint64_t* tags = NULL;
    unsigned char* keys = NULL;
    unsigned char* values = NULL;
    uint64_t* old_tags;
    unsigned char* old_keys;
    unsigned char* old_values;
    size_t old_capacity;
    size_t slot;

    if (ql_size_mul_overflow(new_capacity, sizeof(uint64_t), &tag_bytes) ||
        ql_size_mul_overflow(new_capacity, hash->key_size, &key_bytes) ||
        ql_size_mul_overflow(new_capacity, value_stride, &value_bytes))
    {
        ql_err_raise(QL_ERR_OVERFLOW, "ql_hash_rehash: size overflow");
        return QL_ERR_OVERFLOW;
    }

    tags = ql_alloc(&hash->alloc, tag_bytes);
    keys = ql_alloc(&hash->alloc, key_bytes);
    values = ql_alloc(&hash->alloc, value_bytes);

    if (tags == NULL || keys == NULL || values == NULL)
    {
        ql_dealloc(&hash->alloc, tags);
        ql_dealloc(&hash->alloc, keys);
        ql_dealloc(&hash->alloc, values);
        return QL_ERR_NOMEM;
    }

    /* A zeroed tag array is an empty table. */
    memset(tags, 0, tag_bytes);

    old_tags = hash->tags;
    old_keys = hash->keys;
    old_values = hash->values;
    old_capacity = hash->slot_capacity;

    hash->tags = tags;
    hash->keys = keys;
    hash->values = values;
    hash->slot_capacity = new_capacity;
    hash->tombstones = 0;
    hash->count = 0;

    for (slot = 0; slot < old_capacity; slot++)
    {
        const void* key;
        uint64_t tag;
        ql_hash_probe_result where;

        if (old_tags == NULL || old_tags[slot] < 2)
        {
            continue;
        }

        key = old_keys + (slot * hash->key_size);
        tag = old_tags[slot];
        where = ql_hash_probe(hash, key, tag);

        memcpy(hash->keys + (where.slot * hash->key_size), key, hash->key_size);
        if (hash->value_size != 0)
        {
            memcpy(hash->values + (where.slot * value_stride), old_values + (slot * value_stride), value_stride);
        }
        hash->tags[where.slot] = tag;
        hash->count++;
    }

    ql_dealloc(&hash->alloc, old_tags);
    ql_dealloc(&hash->alloc, old_keys);
    ql_dealloc(&hash->alloc, old_values);
    return QL_OK;
}

/** @brief Whether the table should grow before the next insertion. */
static bool ql_hash_needs_grow(const ql_hash_t* hash)
{
    /* A quarter of the slots stay empty, so probe chains always terminate. */
    size_t limit = (hash->slot_capacity / 4) * 3;

    return hash->count + hash->tombstones + 1 > limit;
}

ql_ret_t ql_hash_create(const ql_allocator_t* alloc, size_t key_size, size_t value_size, ql_hash_t** out)
{
    ql_hash_t* hash;
    ql_ret_t rc;

    if (out == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_hash_create: out is NULL");
        return QL_ERR_INVALID_ARG;
    }
    *out = NULL;

    if (key_size == 0)
    {
        /* With a zero size key every key compares equal, which is never meant. */
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_hash_create: key_size is 0");
        return QL_ERR_INVALID_ARG;
    }

    hash = ql_alloc(alloc, sizeof *hash);
    if (hash == NULL)
    {
        return QL_ERR_NOMEM;
    }

    hash->alloc = alloc != NULL ? *alloc : ql_default_allocator();
    hash->key_size = key_size;
    hash->value_size = value_size;
    hash->slot_capacity = 0;
    hash->count = 0;
    hash->tombstones = 0;
    hash->tags = NULL;
    hash->keys = NULL;
    hash->values = NULL;

    rc = ql_hash_rehash(hash, QL_HASH_MIN_CAPACITY);
    if (rc != QL_OK)
    {
        ql_hash_destroy(hash);
        return rc;
    }

    *out = hash;
    return QL_OK;
}

void ql_hash_destroy(ql_hash_t* hash)
{
    ql_allocator_t alloc;

    if (hash == NULL)
    {
        return;
    }

    /* Copy the allocator out before the control block itself goes away. */
    alloc = hash->alloc;
    ql_dealloc(&alloc, hash->tags);
    ql_dealloc(&alloc, hash->keys);
    ql_dealloc(&alloc, hash->values);
    ql_dealloc(&alloc, hash);
}

ql_ret_t ql_hash_set(ql_hash_t* hash, const void* key, const void* value)
{
    ql_hash_probe_result where;
    size_t value_size;
    uint64_t tag;
    ql_ret_t rc;

    if (hash == NULL || key == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_hash_set: hash or key is NULL");
        return QL_ERR_INVALID_ARG;
    }

    /* Read once: with a non-zero value size a NULL value is a bad argument,
     * and holding the size in a local lets the analyser see that too. */
    value_size = hash->value_size;
    if (value == NULL && value_size != 0)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_hash_set: value is NULL but the table stores values");
        return QL_ERR_INVALID_ARG;
    }

    if (ql_hash_needs_grow(hash))
    {
        size_t next;

        if (ql_size_mul_overflow(hash->slot_capacity, 2, &next))
        {
            ql_err_raise(QL_ERR_OVERFLOW, "ql_hash_set: capacity overflow");
            return QL_ERR_OVERFLOW;
        }
        rc = ql_hash_rehash(hash, next);
        if (rc != QL_OK)
        {
            return rc;
        }
    }

    tag = ql_hash_tag(key, hash->key_size);
    where = ql_hash_probe(hash, key, tag);

    if (!where.found)
    {
        if (hash->tags[where.slot] == 1)
        {
            hash->tombstones--;
        }
        memcpy(hash->keys + (where.slot * hash->key_size), key, hash->key_size);
        hash->count++;
    }

    hash->tags[where.slot] = tag;
    if (value_size != 0)
    {
        memcpy(hash->values + (where.slot * value_size), value, value_size);
    }
    return QL_OK;
}

ql_ret_t ql_hash_get(const ql_hash_t* hash, const void* key, void* out_value)
{
    ql_hash_probe_result where;

    if (hash == NULL || key == NULL || (out_value == NULL && hash->value_size != 0))
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_hash_get: hash, key or out_value is NULL");
        return QL_ERR_INVALID_ARG;
    }

    where = ql_hash_probe(hash, key, ql_hash_tag(key, hash->key_size));
    if (!where.found)
    {
        return QL_ERR_NOT_FOUND;
    }

    if (hash->value_size != 0)
    {
        memcpy(out_value, hash->values + (where.slot * hash->value_size), hash->value_size);
    }
    return QL_OK;
}

bool ql_hash_contains(const ql_hash_t* hash, const void* key)
{
    ql_hash_probe_result where;

    if (hash == NULL || key == NULL)
    {
        return false;
    }

    where = ql_hash_probe(hash, key, ql_hash_tag(key, hash->key_size));
    return where.found;
}

ql_ret_t ql_hash_remove(ql_hash_t* hash, const void* key)
{
    ql_hash_probe_result where;

    if (hash == NULL || key == NULL)
    {
        ql_err_raise(QL_ERR_INVALID_ARG, "ql_hash_remove: hash or key is NULL");
        return QL_ERR_INVALID_ARG;
    }

    where = ql_hash_probe(hash, key, ql_hash_tag(key, hash->key_size));
    if (!where.found)
    {
        return QL_ERR_NOT_FOUND;
    }

    hash->tags[where.slot] = 1;
    hash->count--;
    hash->tombstones++;
    return QL_OK;
}

void ql_hash_clear(ql_hash_t* hash)
{
    if (hash == NULL)
    {
        return;
    }

    if (hash->tags != NULL)
    {
        memset(hash->tags, 0, hash->slot_capacity * sizeof(uint64_t));
    }
    hash->count = 0;
    hash->tombstones = 0;
}

size_t ql_hash_size(const ql_hash_t* hash)
{
    return hash != NULL ? hash->count : 0;
}

size_t ql_hash_capacity(const ql_hash_t* hash)
{
    return hash != NULL ? hash->slot_capacity : 0;
}

size_t ql_hash_key_size(const ql_hash_t* hash)
{
    return hash != NULL ? hash->key_size : 0;
}

size_t ql_hash_value_size(const ql_hash_t* hash)
{
    return hash != NULL ? hash->value_size : 0;
}

void ql_hash_iter_init(ql_hash_iter_t* it, const ql_hash_t* hash)
{
    if (it == NULL)
    {
        return;
    }

    it->hash = hash;
    it->slot = 0;
    it->reserved[0] = NULL;
    it->reserved[1] = NULL;
}

bool ql_hash_iter_next(ql_hash_iter_t* it, const void** out_key, const void** out_value)
{
    const ql_hash_t* hash;

    if (it == NULL || it->hash == NULL)
    {
        return false;
    }
    hash = it->hash;

    while (it->slot < hash->slot_capacity)
    {
        size_t slot = it->slot;

        it->slot++;
        if (hash->tags[slot] < 2)
        {
            continue;
        }

        if (out_key != NULL)
        {
            *out_key = hash->keys + (slot * hash->key_size);
        }
        if (out_value != NULL)
        {
            *out_value = hash->value_size != 0 ? hash->values + (slot * hash->value_size) : NULL;
        }
        return true;
    }

    return false;
}
