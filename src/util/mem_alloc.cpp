#include "util/mem_alloc.h"

#include <esp_heap_caps.h>

namespace Mem {

void *allocPreferSpiram(size_t size) {
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) {
        ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }
    return ptr;
}

} // namespace Mem
