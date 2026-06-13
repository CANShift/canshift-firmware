#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "app_config.h"

namespace Logger {
void init();

void emit(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

bool lockUart(TickType_t timeout);
void unlockUart();
} // namespace Logger

#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_VERBOSE 5

#if APP_LOG_LEVEL >= LOG_LEVEL_ERROR
    #define LOG_ERROR(tag, fmt, ...) ::Logger::emit('E', tag, fmt, ##__VA_ARGS__)
#else
    #define LOG_ERROR(tag, fmt, ...) ((void)0)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_WARN
    #define LOG_WARN(tag, fmt, ...) ::Logger::emit('W', tag, fmt, ##__VA_ARGS__)
#else
    #define LOG_WARN(tag, fmt, ...) ((void)0)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_INFO
    #define LOG_INFO(tag, fmt, ...) ::Logger::emit('I', tag, fmt, ##__VA_ARGS__)
#else
    #define LOG_INFO(tag, fmt, ...) ((void)0)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_DEBUG
    #define LOG_DEBUG(tag, fmt, ...) ::Logger::emit('D', tag, fmt, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(tag, fmt, ...) ((void)0)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_DEBUG && APP_VERBOSE_DEBUG_LOGS
    #define LOG_VDEBUG(tag, fmt, ...) ::Logger::emit('D', tag, fmt, ##__VA_ARGS__)
#else
    #define LOG_VDEBUG(tag, fmt, ...) ((void)0)
#endif

#if APP_LOG_LEVEL >= LOG_LEVEL_VERBOSE
    #define LOG_VERBOSE(tag, fmt, ...) ::Logger::emit('V', tag, fmt, ##__VA_ARGS__)
#else
    #define LOG_VERBOSE(tag, fmt, ...) ((void)0)
#endif
