#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

namespace canshift_test {
inline uint32_t &mockMillisRef() {
    static uint32_t value = 0;
    return value;
}
}

inline uint32_t millis() {
    return canshift_test::mockMillisRef();
}

inline void mockSetMillis(uint32_t value) {
    canshift_test::mockMillisRef() = value;
}

inline void mockAdvanceMillis(uint32_t delta) {
    canshift_test::mockMillisRef() += delta;
}

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
}

class HardwareSerialMock : public Print {
  public:
    size_t write(uint8_t b) override {
        return write(&b, 1);
    }
    size_t write(const uint8_t *data, size_t size) override {
        char *buf = canshift_test::serialCaptureBuffer();
        size_t &len = canshift_test::serialCaptureLen();
        const size_t cap = 4096 - 1;
        if (len >= cap)
            return 0;
        const size_t avail = cap - len;
        const size_t take = (size < avail) ? size : avail;
        memcpy(buf + len, data, take);
        len += take;
        buf[len] = '\0';
        return take;
    }
    int availableForWrite() {
        const size_t cap = 4096 - 1;
        const size_t len = canshift_test::serialCaptureLen();
        return static_cast<int>(len >= cap ? 0 : cap - len);
    }
    void flush() {}
};

inline HardwareSerialMock &serialInstance() {
    static HardwareSerialMock s;
    return s;
}

#define Serial serialInstance()

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
