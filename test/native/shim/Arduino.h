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

// ---------------------------------------------------------------------------
// Capturing HardwareSerial stand-in. Logger emits via `Serial.write(buf,n)`;
// host tests that exercise the logger need to (a) compile against the
// `Serial` symbol and (b) inspect the bytes that were written. Both
// requirements are satisfied by a tiny in-memory buffer collector — see
// test_logger (#1037). The default implementation is a no-op for tests
// that do not care.
// ---------------------------------------------------------------------------
namespace canshift_test {
inline char *serialCaptureBuffer() {
    static char buf[4096];
    return buf;
}
inline size_t &serialCaptureLen() {
    static size_t len = 0;
    return len;
}
inline void serialCaptureReset() {
    serialCaptureLen() = 0;
    serialCaptureBuffer()[0] = '\0';
}
} // namespace canshift_test

class HardwareSerialMock : public Print {
  public:
    size_t write(uint8_t b) override {
        return write(&b, 1);
    }
    size_t write(const uint8_t *data, size_t size) override {
        char *buf = canshift_test::serialCaptureBuffer();
        size_t &len = canshift_test::serialCaptureLen();
        const size_t cap = 4096 - 1; // reserve \0
        if (len >= cap)
            return 0;
        const size_t avail = cap - len;
        const size_t take = (size < avail) ? size : avail;
        memcpy(buf + len, data, take);
        len += take;
        buf[len] = '\0';
        return take;
    }
};

inline HardwareSerialMock &serialInstance() {
    static HardwareSerialMock s;
    return s;
}

#define Serial serialInstance()

// glibc 2.38+ provides strlcpy natively (exposed by _FORTIFY_SOURCE under -O1+);
// BSD/macOS libc has always had it. Only define the shim on older glibc where
// the symbol is genuinely missing, otherwise the inline collides with the libc
// declaration (issue #830 surfaced this when ASan flipped -O0 -> -O1).
#ifdef __APPLE__
    #define CANSHIFT_HAVE_STRLCPY 1
#elif defined(__GLIBC__)
    #include <features.h>
    #if defined(__GLIBC_PREREQ) && __GLIBC_PREREQ(2, 38)
        #define CANSHIFT_HAVE_STRLCPY 1
    #endif
#endif

#ifndef CANSHIFT_HAVE_STRLCPY
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
