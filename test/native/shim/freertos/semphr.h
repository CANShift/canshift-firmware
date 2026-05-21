#pragma once
// freertos/semphr.h shim — single-threaded host build needs no real mutex,
// but recursive-mutex tests (e.g. test_logger #1037) need a counter so we can
// observe re-entrant Take/Give pairs and catch a regression where production
// code accidentally swaps the recursive APIs for the binary ones.

#include "freertos/FreeRTOS.h"

#include <cstdlib>
#include <stdint.h>

using SemaphoreHandle_t = void *;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    static int sentinel = 1;
    return &sentinel;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t /*sem*/, TickType_t /*timeout*/) {
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t /*sem*/) {
    return pdTRUE;
}

// ---------------------------------------------------------------------------
// Recursive mutex — tracks depth so tests can pin down the contract that
// Logger::lockUart followed by a re-entrant LOG_* never deadlocks (#1037).
// Single global counter is sufficient because the host test runs
// single-threaded; if a test ever spawns a thread we'll need a real mutex.
// ---------------------------------------------------------------------------

namespace canshift_test {
inline int32_t &recursiveMutexDepth() {
    static int32_t depth = 0;
    return depth;
}
} // namespace canshift_test

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    static int recursiveSentinel = 1;
    canshift_test::recursiveMutexDepth() = 0;
    return &recursiveSentinel;
}

inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t /*sem*/, TickType_t /*timeout*/) {
    canshift_test::recursiveMutexDepth()++;
    return pdTRUE;
}

inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t /*sem*/) {
    if (canshift_test::recursiveMutexDepth() <= 0) {
        // Unbalanced Give — production behaviour is undefined; abort the
        // test so the regression surfaces loudly rather than silently
        // corrupting the depth counter.
        std::abort();
    }
    canshift_test::recursiveMutexDepth()--;
    return pdTRUE;
}
