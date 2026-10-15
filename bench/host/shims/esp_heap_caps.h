#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstdint>

#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA 0

inline size_t heap_caps_get_largest_free_block(uint32_t) {
    return 4u * 1024u * 1024u;
}

inline size_t heap_caps_get_free_size(uint32_t) {
    return 8u * 1024u * 1024u;
}

inline size_t heap_caps_get_total_size(uint32_t) {
    return 16u * 1024u * 1024u;
}

inline void *heap_caps_malloc(size_t size, uint32_t) {
    return malloc(size);
}

inline void *heap_caps_realloc(void *ptr, size_t size, uint32_t) {
    return realloc(ptr, size);
}

inline void *heap_caps_calloc(size_t count, size_t size, uint32_t) {
    return calloc(count, size);
}

inline void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
    return aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
}

inline void heap_caps_free(void *ptr) {
    free(ptr);
}
