#pragma once
// logger.h — Leveled serial logging
//
// Usage:
//   LOG_INFO("TAG", "Message with value: %d", value);
//   LOG_WARN("CAN",  "Frame timeout on ID 0x%03X", frameId);
//   LOG_ERROR("UI",  "Widget factory returned null for type: %s", type);
//   LOG_DEBUG("SIG", "Signal %s = %.2f", name, value);

#include <Arduino.h>
#include "app_config.h"

namespace Logger {
void init();
}

// Log levels
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_VERBOSE 5

// Logging macros — compile out levels above APP_LOG_LEVEL
#if APP_LOG_LEVEL >= LOG_LEVEL_ERROR
    #define LOG_ERROR(tag, fmt, ...) Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_ERROR(tag, fmt, ...)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_WARN
    #define LOG_WARN(tag, fmt, ...) Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_WARN(tag, fmt, ...)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_INFO
    #define LOG_INFO(tag, fmt, ...) Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_INFO(tag, fmt, ...)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_DEBUG
    #define LOG_DEBUG(tag, fmt, ...) Serial.printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(tag, fmt, ...)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_VERBOSE
    #define LOG_VERBOSE(tag, fmt, ...) Serial.printf("[V][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define LOG_VERBOSE(tag, fmt, ...)
#endif
