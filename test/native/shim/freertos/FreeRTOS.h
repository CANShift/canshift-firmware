#pragma once

#include <stdint.h>

#include "../mock_clock.h"

using TickType_t = uint32_t;
using BaseType_t = int32_t;

#define pdTRUE 1
#define pdFALSE 0

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))

inline void vTaskDelay(TickType_t ticks) {
    canshift_test::mockMillisRef() += ticks;
}

#define portMAX_DELAY 0xFFFFFFFFu
