#pragma once

#include "freertos/FreeRTOS.h"
#include "bench_clock.h"

using TaskHandle_t = void *;
using StackType_t = uint8_t;
struct StaticTask_t {
    int dummy;
};

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    return nullptr;
}

inline const char *pcTaskGetName(TaskHandle_t) {
    return "sim";
}

inline void vTaskDelay(TickType_t ticks) {
    BenchClock::sleepMs(ticks);
}

inline TickType_t xTaskGetTickCount() {
    return BenchClock::nowMs();
}

inline void vTaskDelayUntil(TickType_t *prev, TickType_t period) {
    *prev += period;
    vTaskDelay(period);
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) {
    return 4096;
}
