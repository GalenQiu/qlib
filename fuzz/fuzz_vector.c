#include "qlib/ql_vector.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * libFuzzer entry point.
 *
 * The input bytes are treated as a script of vector operations. The fuzzer is
 * looking for two things in particular:
 *
 *   - memory errors on the failure paths, where a grow fails part way through
 *     an operation and the vector must be left consistent;
 *   - invariant violations, checked after every single operation.
 *
 * Every payload byte drives one operation, so inputs stay small and the
 * interesting states are reached quickly.
 */

/* Upper bound on the element size the payload may ask for. */
#define FUZZ_MAX_ELEM_SIZE 32

/* Keeps the generated indices small: huge allocations would just be slow. */
#define FUZZ_MAX_INDEX 64

/* Declared up front so -Wmissing-prototypes stays quiet on the definition. */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static int check_invariants(ql_vector_t* vec)
{
    size_t count = ql_vector_size(vec);
    size_t capacity = ql_vector_capacity(vec);
    size_t i;

    if (count > capacity)
    {
        return 0;
    }

    /* Every live slot must be addressable. */
    for (i = 0; i < count; i++)
    {
        if (ql_vector_at(vec, i) == NULL)
        {
            return 0;
        }
    }

    /* One past the end must never be addressable. */
    if (ql_vector_at(vec, count) != NULL)
    {
        return 0;
    }

    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    ql_allocator_t alloc;
    ql_vector_t* vec = NULL;
    uint8_t elem[FUZZ_MAX_ELEM_SIZE];
    size_t elem_size;
    size_t i;
    size_t j;

    if (size < 2)
    {
        return 0;
    }

    /* Element size is derived from the input so odd sizes get exercised too. */
    elem_size = (size_t)(data[0] % FUZZ_MAX_ELEM_SIZE) + 1;

    /*
     * The source element must be at least elem_size bytes. Passing a smaller
     * object here would make ql_vector_insert read off the end of a stack
     * buffer, which is a bug in this harness rather than in the library.
     */
    for (j = 0; j < elem_size; j++)
    {
        elem[j] = data[(j % (size - 1)) + 1];
    }

    alloc = ql_default_allocator();
    if (ql_vector_create(&alloc, elem_size, &vec) != QL_OK)
    {
        return 0;
    }

    for (i = 1; i < size; i++)
    {
        uint8_t op = data[i] % 8;
        size_t index = data[i] % FUZZ_MAX_INDEX;
        size_t count = ql_vector_size(vec);

        switch (op)
        {
        case 0:
            (void)ql_vector_push_back(vec, elem);
            break;
        case 1:
            (void)ql_vector_pop_back(vec, NULL);
            break;
        case 2:
            (void)ql_vector_insert(vec, index, elem);
            break;
        case 3:
            (void)ql_vector_remove(vec, index);
            break;
        case 4:
            (void)ql_vector_reserve(vec, index);
            break;
        case 5:
            (void)ql_vector_resize(vec, index);
            break;
        case 6:
        {
            /* Feed the vector its own memory: the aliasing path. */
            void* slot = ql_vector_at(vec, count == 0 ? 0 : index % count);
            if (slot != NULL)
            {
                (void)ql_vector_push_back(vec, slot);
            }
            break;
        }
        default:
            if (count > 0)
            {
                size_t at = index % count;
                void* slot = ql_vector_at(vec, at);
                if (slot != NULL)
                {
                    /* Overlapping self-assignment: exercises the memmove. */
                    (void)ql_vector_assign(vec, at, slot);
                }
            }
            break;
        }

        if (!check_invariants(vec))
        {
            abort();
        }
    }

    ql_vector_destroy(vec);
    return 0;
}
