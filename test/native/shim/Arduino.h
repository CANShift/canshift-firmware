#pragma once
// Arduino.h shim for native (host) unit tests.
//
// Production code reaches for `Arduino.h` to get `millis()` and a handful of
// printf-style helpers. The host build replaces those with deterministic
// stand-ins so tests can advance time precisely. No `String`, no `Serial`,
// no PROGMEM — production code in our scope (signal_store, config_loader,
// can_parser, json_reader) does not need them.

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Mocked time source — backed by a single counter the test controls.
// ---------------------------------------------------------------------------
namespace canshift_test {
inline uint32_t &mockMillisRef() {
    static uint32_t value = 0;
    return value;
}
} // namespace canshift_test

inline uint32_t millis() {
    return canshift_test::mockMillisRef();
}

inline void mockSetMillis(uint32_t value) {
    canshift_test::mockMillisRef() = value;
}

inline void mockAdvanceMillis(uint32_t delta) {
    canshift_test::mockMillisRef() += delta;
}

// ---------------------------------------------------------------------------
// Bounded string copy — production uses `strlcpy` from <bsd/string.h> on the
// ESP32 toolchain. Provide a portable equivalent for the host build.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Minimal Print stub — the production `Print` class is the base of Arduino's
// `Stream` / `HardwareSerial`. The storage driver header references it for
// streamFileTo(); host tests never exercise that path but the type must
// resolve so the header compiles.
// ---------------------------------------------------------------------------
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) {
        return 0;
    }
    virtual size_t write(const uint8_t *, size_t size) {
        return size;
    }
};

#ifndef strlcpy
inline size_t strlcpy(char *dst, const char *src, size_t dstSize) {
    if (dstSize == 0)
        return strlen(src);
    size_t srcLen = strlen(src);
    size_t copyLen = (srcLen >= dstSize) ? dstSize - 1 : srcLen;
    memcpy(dst, src, copyLen);
    dst[copyLen] = '\0';
    return srcLen;
}
#endif
