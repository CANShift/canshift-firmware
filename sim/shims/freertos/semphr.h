#pragma once

#include "freertos/FreeRTOS.h"

using SemaphoreHandle_t = void *;

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    static int sentinel = 1;
    return &sentinel;
}

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
    static int sentinel = 2;
    return &sentinel;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) {
    return pdTRUE;
}
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) {
    return pdTRUE;
}
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) {
    return pdTRUE;
}
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) {
    return pdTRUE;
}

inline void *xSemaphoreGetMutexHolder(SemaphoreHandle_t) {
    return nullptr;
}
