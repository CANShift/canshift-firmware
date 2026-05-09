#pragma once
// freertos/semphr.h shim — single-threaded host build needs no real mutex.
// Functions return success unconditionally. The handle is an opaque non-null
// sentinel so callers' null-checks succeed.

#include "freertos/FreeRTOS.h"

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
