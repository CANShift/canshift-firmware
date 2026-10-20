#pragma once

#include <esp_heap_caps.h>
#include <memory>
#include <stddef.h>

namespace Mem {

void *allocPreferSpiram(size_t size);

struct HeapFree {
    void operator()(void *block) const {
        heap_caps_free(block);
    }
};

template <typename T>
using HeapPtr = std::unique_ptr<T, HeapFree>;

}
