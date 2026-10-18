#pragma once

#include <cstdint>
#include <cstdlib>

namespace BenchRestart {

using Hook = void (*)();

inline Hook &hook() {
    static Hook installed = nullptr;
    return installed;
}

}

namespace BenchRandom {

inline constexpr uint32_t kStep = 0x9E3779B9u;

inline uint32_t next() {
    static uint32_t value = 0;
    value += kStep;
    return value;
}

}

inline void esp_restart() {
    if (BenchRestart::hook() == nullptr)
        exit(0);
    BenchRestart::hook()();
}

inline uint32_t esp_random() {
    return BenchRandom::next();
}
