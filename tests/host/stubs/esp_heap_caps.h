#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_8BIT 2U
#ifdef HOST_TRACK_HEAP
extern unsigned host_heap_allocations;
#endif
static inline void *heap_caps_malloc(size_t size,unsigned caps) {
    (void)caps;
#ifdef HOST_TRACK_HEAP
    ++host_heap_allocations;
#endif
    return malloc(size);
}
