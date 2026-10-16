#include "qlib/ql_common.h"

/* <stdlib.h> declares malloc/realloc/free on every supported platform. */
#include <stdlib.h>

static void* ql_def_malloc(size_t size, void* userdata)
{
    (void)userdata;
    return malloc(size);
}

static void* ql_def_realloc(void* ptr, size_t size, void* userdata)
{
    (void)userdata;
    return realloc(ptr, size);
}

static void ql_def_free(void* ptr, void* userdata)
{
    (void)userdata;
    free(ptr);
}

ql_allocator_t ql_default_allocator(void)
{
    ql_allocator_t alloc;
    alloc.malloc = ql_def_malloc;
    alloc.realloc = ql_def_realloc;
    alloc.free = ql_def_free;
    alloc.userdata = NULL;
    return alloc;
}
