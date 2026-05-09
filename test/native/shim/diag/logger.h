#pragma once
// diag/logger.h shim — silence every LOG_* call in host tests.
//
// Production code dispatches log calls through these macros and the
// `Logger::emit` symbol. Tests don't care about output; reducing the macros
// to `((void)0)` also avoids dragging the real Logger TU (which pulls in
// FreeRTOS mutexes and the UART) into the host build.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define LOG_ERROR(tag, fmt, ...) ((void)0)
#define LOG_WARN(tag, fmt, ...) ((void)0)
#define LOG_INFO(tag, fmt, ...) ((void)0)
#define LOG_DEBUG(tag, fmt, ...) ((void)0)
#define LOG_VERBOSE(tag, fmt, ...) ((void)0)

namespace Logger {
inline void init() {}
inline void emit(char /*level*/, const char * /*tag*/, const char * /*fmt*/, ...) {}
inline bool lockUart(TickType_t /*timeout*/) {
    return true;
}
inline void unlockUart() {}
} // namespace Logger
