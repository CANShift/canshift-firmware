#pragma once
// freertos/FreeRTOS.h shim for host unit tests.
//
// Tests run single-threaded on the host. FreeRTOS task/scheduler primitives
// are reduced to no-ops. Only the surface area touched by the modules under
// test (signal_store, config_loader) is provided.

#include <stdint.h>

using TickType_t = uint32_t;
using BaseType_t = int32_t;

#define pdTRUE 1
#define pdFALSE 0

#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))

#define portMAX_DELAY 0xFFFFFFFFu
