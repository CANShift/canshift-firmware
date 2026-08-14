#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

inline uint32_t millis() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

inline uint32_t micros() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<uint32_t>(duration_cast<microseconds>(steady_clock::now() - start).count());
}

inline void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline long random(long minVal, long maxVal) {
    if (maxVal <= minVal)
        return minVal;
    return minVal + (rand() % (maxVal - minVal));
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
    int availableForWrite() {
        return 4096;
    }
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
