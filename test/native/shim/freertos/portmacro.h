#pragma once

#include "freertos/FreeRTOS.h"

struct PortMuxStub {};

using portMUX_TYPE = PortMuxStub;

#define portMUX_INITIALIZER_UNLOCKED                                                               \
    {}

#define portENTER_CRITICAL(mux) (void)(mux)
#define portEXIT_CRITICAL(mux) (void)(mux)
