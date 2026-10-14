#pragma once

#include "FreeRTOS.h"

using TaskHandle_t = void *;

inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    static int s_hostTask = 0;
    return &s_hostTask;
}
