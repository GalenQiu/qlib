#include "qlib/ql_mem.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * libFuzzer entry point for the memory module.
 *
 * The interesting surface here is arithmetic, not parsing: the padded size, the
 * alignment rounding, and the two overflow helpers. The payload therefore picks
 * alignment and size values directly, including deliberately invalid ones, and
 * the interesting properties are checked on every iteration:
 *
 *   - a non-NULL aligned block really is aligned, and all `size` bytes of it are
 *     writable (ASAN catches a layout that would overrun);
 *   - an invalid alignment is rejected rather than producing a wild pointer;
 *   - the overflow helpers agree with the arithmetic they are guarding.
 */

/* Keeps generated sizes small: the fuzzer should explore state, not RAM. */
#define FUZZ_MAX_PAYLOAD 4096U
#define FUZZ_MAX_ALIGN_EXP 17U

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static uint32_t read_u32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static void exercise_aligned(const uint8_t* data)
{
    size_t alignment;
    size_t payload;
    unsigned char* p;
    bool expect_accepted;

    payload = (size_t)(read_u32(data + 5) % FUZZ_MAX_PAYLOAD);

    if ((data[0] & 1U) != 0U)
    {
        /* A valid power of two, small enough that the padding stays sane. */
        alignment = (size_t)1U << (data[1] % FUZZ_MAX_ALIGN_EXP);
        expect_accepted = true;
    }
    else
    {
        /*
         * OR-ing in both low bits guarantees the result is not a power of two,
         * so this alignment must be refused.
         *
         * This mask is load-bearing. Without it a raw 32-bit value occasionally
         * lands on a power of two -- 0x80000000, for instance -- and the library
         * then correctly accepts it and legitimately attempts to allocate one
         * alignment-sized slab, which is 2 GiB. That is the documented cost, not
         * a defect; the harness simply must not ask for it.
         */
        alignment = (size_t)read_u32(data + 1) | 3U;
        expect_accepted = false;
    }

    p = ql_aligned_alloc(NULL, alignment, payload);

    if (!expect_accepted)
    {
        if (p != NULL)
        {
            abort(); /* a non-power-of-two alignment must never be accepted */
        }
        return;
    }

    if (p == NULL)
    {
        return; /* out of memory is allowed */
    }

    if (payload != 0U)
    {
        if (((uintptr_t)p % (uintptr_t)alignment) != 0U)
        {
            abort(); /* the whole point of the function */
        }

        memset(p, 0xAB, payload);
        if (p[payload - 1U] != 0xAB)
        {
            abort();
        }
    }

    ql_aligned_free(NULL, p);
}

static void exercise_arrays(const uint8_t* data)
{
    size_t count = (size_t)(read_u32(data) % 32U);
    size_t elem = (size_t)(read_u32(data + 4) % 64U);
    size_t total = 0;
    void* block;

    /* The helpers must agree with the arithmetic they guard. */
    if (ql_size_mul_overflow(count, elem, &total) != QL_OK)
    {
        abort(); /* these operands cannot overflow */
    }
    if (total != count * elem)
    {
        abort();
    }

    block = ql_alloc_array(NULL, count, elem);
    if (block != NULL)
    {
        if (total != 0U)
        {
            memset(block, 0x5A, total);
        }
        ql_dealloc(NULL, block);
    }

    /* An overflowing request must be refused, not wrapped. */
    total = 0;
    if (ql_size_mul_overflow(SIZE_MAX, 2, &total) != QL_ERR_OVERFLOW)
    {
        abort();
    }
    if (ql_size_add_overflow(SIZE_MAX, 1, &total) != QL_ERR_OVERFLOW)
    {
        abort();
    }
    if (ql_alloc_array(NULL, SIZE_MAX, 2) != NULL)
    {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 9)
    {
        return 0;
    }

    exercise_aligned(data);
    exercise_arrays(data);

    ql_aligned_free(NULL, NULL); /* must stay a no-op */
    ql_dealloc(NULL, NULL);

    return 0;
}
