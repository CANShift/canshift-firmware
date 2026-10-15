#pragma once

#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY 0xFFFFFFFFu

#ifndef configASSERT
    #define configASSERT(x) ((void)(x))
#endif

struct portMUX_TYPE {
    int dummy;
};
#define portMUX_INITIALIZER_UNLOCKED {0}

inline void vPortEnterCritical(portMUX_TYPE *) {}
inline void vPortExitCritical(portMUX_TYPE *) {}
#define portENTER_CRITICAL(mux) vPortEnterCritical(mux)
#define portEXIT_CRITICAL(mux) vPortExitCritical(mux)
#define taskENTER_CRITICAL(mux) vPortEnterCritical(mux)
#define taskEXIT_CRITICAL(mux) vPortExitCritical(mux)

inline int xPortGetCoreID() {
    return 0;
}
