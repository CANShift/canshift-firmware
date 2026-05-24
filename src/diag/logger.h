#pragma once
// logger.h — Leveled serial logging emitted as JSON-line envelopes
//
// Usage:
//   LOG_INFO("TAG", "Message with value: %d", value);
//   LOG_WARN("CAN",  "Frame timeout on ID 0x%03X", frameId);
//   LOG_ERROR("UI",  "Widget factory returned null for type: %s", type);
//   LOG_DEBUG("SIG", "Signal %s = %.2f", name, value);
//
// Wire format (one line per call, terminated by \n):
//   {"log":1,"lvl":"E|W|I|D|V","tag":"<≤16 chars>","msg":"<escaped message>"}
//
// All UART0 writes from the wire-protocol producer (USB task) MUST also be
// serialized via Logger::lockUart()/unlockUart() so log lines can never
// fragment a JSON frame mid-emission. Both ends of the pair are required
// because logger emit() and the USB writer can run on different cores.
//
// The underlying mutex is recursive: a task that already holds it via
// lockUart() may still call LOG_* without deadlocking. Each lockUart() must
// be balanced by exactly one unlockUart() on the same task.
//
// Reentry safety has two regimes (F-HI-6 / F-ME-9, umbrella #1014):
//   - After Logger::init() has run, the recursive mutex serializes every
//     writer and lets a task re-enter emit() from a nested call site.
//   - Before Logger::init() runs (pre-init window in setup() between
//     Serial.begin and Logger::init), the mutex is null and the internal
//     static buffers are unprotected. The only writer allowed in that
//     window is the Arduino loopTask on TASK_CORE_UI — emit() asserts this
//     at runtime so a stray pre-init caller from another task surfaces
//     immediately instead of silently corrupting the buffers.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "app_config.h"

namespace Logger {
void init();

// Emit a single log line as a JSON envelope on UART0. Drops the line silently
// if the UART mutex cannot be taken within 50 ms (avoids deadlocks).
void emit(char level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

// Acquire / release the UART0 serialization mutex. Wire-protocol producers
// (e.g. UsbComm) must bracket multi-call frames with these so a logger emit()
// from another task can't slip in the middle of a JSON line. The mutex is
// recursive — re-entering from the same task (e.g. LOG_ERROR from an assert
// handler nested inside a streamFileTo block) is safe; calls must still be
// balanced 1:1.
//
// NOTE: pre-Logger::init() boot text from the Arduino core / ESP-IDF can still
// land on UART0 — the studio's safeJsonParse + isRecord guards drop those
// silently, so this is non-fatal. Not addressed here.
bool lockUart(TickType_t timeout);
void unlockUart();
} // namespace Logger

// Log levels
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_VERBOSE 5

// Logging macros — compile out levels above APP_LOG_LEVEL
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
