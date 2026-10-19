#pragma once

#include "freertos/FreeRTOS.h"

#include <cstdlib>
#include <stdint.h>

using SemaphoreHandle_t = void *;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    static int sentinel = 1;
    return &sentinel;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) {
    return pdTRUE;
}

namespace canshift_test {
inline int32_t &recursiveMutexDepth() {
    static int32_t depth = 0;
    return depth;
}
}

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    static int recursiveSentinel = 1;
    canshift_test::recursiveMutexDepth() = 0;
    return &recursiveSentinel;
}

inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) {
    canshift_test::recursiveMutexDepth()++;
    return pdTRUE;
}

inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) {
    if (canshift_test::recursiveMutexDepth() <= 0) {
        std::abort();
    }
    canshift_test::recursiveMutexDepth()--;
    return pdTRUE;
}
