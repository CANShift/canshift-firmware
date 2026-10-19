#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace Logger {
inline void init() {}
inline void emit(char, const char *, const char *, ...) {}
bool writeAll(const uint8_t *bytes, size_t len);
class UartLock {
  public:
    explicit UartLock(TickType_t) {}
    bool held() const {
        return true;
    }
};
}

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
