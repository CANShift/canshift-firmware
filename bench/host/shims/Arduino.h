#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bench_clock.h"
#include "esp_system.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>

inline uint32_t millis() {
    return BenchClock::nowMs();
}

inline uint32_t micros() {
    return static_cast<uint32_t>(BenchClock::nowUs());
}

inline void delay(uint32_t ms) {
    BenchClock::sleepMs(ms);
}

inline long random(long minVal, long maxVal) {
    if (maxVal <= minVal)
        return minVal;
    return minVal + static_cast<long>(BenchRandom::next() % static_cast<uint32_t>(maxVal - minVal));
}

#ifndef LOW
    #define LOW 0
    #define HIGH 1
    #define INPUT 0
    #define INPUT_PULLUP 2
#endif

inline void pinMode(int, int) {}
inline int digitalRead(int) {
    return HIGH;
}

class Print {
  public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t c) {
        return fwrite(&c, 1, 1, stdout);
    }
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        for (size_t i = 0; i < size; ++i)
            n += write(buffer[i]);
        return n;
    }
    size_t print(const char *s) {
        return write(reinterpret_cast<const uint8_t *>(s), strlen(s));
    }
};

class SerialShim : public Print {
  public:
    using Capture = void (*)(uint8_t c);
    void setCapture(Capture capture) {
        m_capture = capture;
    }
    size_t write(uint8_t c) override {
        if (m_capture == nullptr)
            return Print::write(c);
        m_capture(c);
        return 1;
    }
    using Print::write;
    void begin(unsigned long) {}
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
    }
    void println(const char *s = "") {
        ::printf("%s\n", s);
    }
    void flush() {}
    int available() {
        return 0;
    }
    int read() {
        return -1;
    }
    int availableForWrite() {
        return 4096;
    }

  private:
    Capture m_capture = nullptr;
};

inline SerialShim Serial;

class EspShim {
  public:
    uint32_t getFreeHeap() {
        return 8u * 1024u * 1024u;
    }
    uint32_t getMinFreeHeap() {
        return 4u * 1024u * 1024u;
    }
};

inline EspShim ESP;
