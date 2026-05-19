#pragma once
// freertos/portmacro.h shim — host unit tests run single-threaded, so the
// portMUX spinlock collapses to nothing. The macros are deliberately empty
// (no `{}`) so the caller's `portENTER_CRITICAL(&mux); ...; portEXIT_CRITICAL(&mux);`
// pair leaves the surrounding code untouched.

#include "freertos/FreeRTOS.h"

struct PortMuxStub {};

using portMUX_TYPE = PortMuxStub;

#define portMUX_INITIALIZER_UNLOCKED                                                               \
    {}

#define portENTER_CRITICAL(mux) (void)(mux)
#define portEXIT_CRITICAL(mux) (void)(mux)
