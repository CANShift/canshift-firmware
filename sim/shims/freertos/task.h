#pragma once

#include "freertos/FreeRTOS.h"

#include <chrono>
#include <thread>

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
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

inline TickType_t xTaskGetTickCount() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<TickType_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}

inline void vTaskDelayUntil(TickType_t *prev, TickType_t period) {
    *prev += period;
    vTaskDelay(period);
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) {
    return 4096;
}
