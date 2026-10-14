#pragma once

#include <cstdint>

namespace SimRandom {

inline constexpr uint32_t kSeed = 0x2545F491u;

inline uint32_t &state() {
    static uint32_t value = kSeed;
    return value;
}

inline uint32_t next() {
    uint32_t x = state();
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state() = x;
    return x;
}

} // namespace SimRandom
