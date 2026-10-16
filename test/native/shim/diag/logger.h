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

namespace Logger {
inline void init() {}
inline void emit(char /*level*/, const char * /*tag*/, const char * /*fmt*/, ...) {}
bool writeAll(const uint8_t *bytes, size_t len);
inline bool lockUart(TickType_t /*timeout*/) {
    return true;
}
inline void unlockUart() {}
} // namespace Logger

#define LOG_DISCARD(tag, fmt, ...)                                                                 \
    do {                                                                                           \
        if (false)                                                                                 \
            Logger::emit('D', tag, fmt, ##__VA_ARGS__);                                            \
    } while (0)
#define LOG_ERROR(tag, fmt, ...) LOG_DISCARD(tag, fmt, ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...) LOG_DISCARD(tag, fmt, ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...) LOG_DISCARD(tag, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(tag, fmt, ...) LOG_DISCARD(tag, fmt, ##__VA_ARGS__)
#define LOG_VDEBUG(tag, fmt, ...) LOG_DISCARD(tag, fmt, ##__VA_ARGS__)
#define LOG_VERBOSE(tag, fmt, ...) LOG_DISCARD(tag, fmt, ##__VA_ARGS__)
