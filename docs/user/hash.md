# Hash table

`<qlib/ql_hash.h>` provides `ql_hash_t`: a hash table mapping fixed size keys to
fixed size values. Both are raw byte blocks whose size is fixed at creation,
the same idea as `ql_vector`'s `elem_size`. Keys and values are copied in and
out, so nothing has to be freed by hand and the caller's memory is never
retained.

Lookup is amortised O(1): open addressing with linear probing over a
power-of-two slot array, kept below a 75% load factor.

## Basic use

```c
ql_hash_t* squares = NULL;
int i;

if (ql_hash_create(NULL, sizeof(int), sizeof(int), &squares) != QL_OK) {
    return 1;
}
for (i = 1; i <= 5; i++) {
    int result = i * i;
    if (ql_hash_set(squares, &i, &result) != QL_OK) {
        ql_hash_destroy(squares);
        return 1;
    }
}

{
    int key = 4;
    int found = 0;

    if (ql_hash_get(squares, &key, &found) == QL_OK) {
        printf("4 squared is %d\n", found);
    }
}
printf("the table holds %zu entries\n", ql_hash_size(squares));
```

```
4 squared is 16
the table holds 5 entries
```

`ql_hash_set()` inserts or overwrites: setting an existing key replaces its value
and leaves the entry count unchanged.

## Keys are compared as bytes

The whole key block is compared with `memcmp`, which has consequences worth
knowing before choosing a key type:

| Key type | Works? | Note |
|---|---|---|
| Integer | Yes | No padding to worry about |
| Struct without padding | Yes | Build it with an initialiser, not `memset` plus field writes |
| Struct **with** padding | Dangerous | Padding bytes are part of the key; two values that look equal may not match |
| Fixed size `char[8]` | Yes | The unused bytes must be zeroed by the caller, or "alpha" and "alpha\0\0\0garbage" differ |
| `char*` | No | That hashes the pointer, not the text |

For string keys, zero the buffer first:

```c
char key[16];

memset(key, 0, sizeof key);
snprintf(key, sizeof key, "%s", word);
ql_hash_set(table, key, &value);
```

A key size of 0 is rejected at creation: every key would compare equal, which is
never what the caller meant.

## A membership set

Passing a value size of 0 turns the table into a set, which only answers "is this
key present?":

```c
ql_hash_t* seen = NULL;
int id = 7;

ql_hash_create(NULL, sizeof id, 0, &seen);
ql_hash_set(seen, &id, NULL);            /* no value to pass */
bool there = ql_hash_contains(seen, &id);
ql_hash_get(seen, &id, NULL);            /* QL_OK, nothing copied */
```

## Removing

```c
if (ql_hash_remove(table, &key) == QL_ERR_NOT_FOUND) {
    /* it was not there */
}
```

Removal writes a tombstone rather than shifting entries, so a key living further
along another key's probe chain stays findable. Tombstones are reused by later
inserts and cleared on rehash.

## Iterating

```c
ql_hash_iter_t it;
const void* raw_key;
const void* raw_value;

ql_hash_iter_init(&it, table);
while (ql_hash_iter_next(&it, &raw_key, &raw_value)) {
    int key;
    int value;

    memcpy(&key, raw_key, sizeof key);
    memcpy(&value, raw_value, sizeof value);
    printf("%d -> %d\n", key, value);
}
```

The iterator hands back pointers into the table's own storage, valid until the
table is next modified. Copy what you need out. **Any modification invalidates
the iterator**: start again with `ql_hash_iter_init()`.

No ordering is promised, and removal changes it.

## Performance

| Operation | Cost |
|---|---|
| `ql_hash_set` (including growth) | 61 ns |
| `ql_hash_get` (4096 entries) | 18 ns |

## Error codes

| Situation | Code |
|---|---|
| `NULL` table or key; `NULL` value when the table stores values; `key_size` 0 | `QL_ERR_INVALID_ARG` |
| Allocation failed | `QL_ERR_NOMEM` |
| Capacity would overflow `size_t` | `QL_ERR_OVERFLOW` |
| `ql_hash_get()` / `ql_hash_remove()` on an absent key | `QL_ERR_NOT_FOUND` |

`QL_ERR_NOT_FOUND` is an ordinary answer, not a fault: it is returned but not
reported to the [error handler](errors.md). `ql_hash_contains()` answers the same
question without an error code at all.
