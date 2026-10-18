#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace SimClock {

inline constexpr int64_t kMicrosPerMilli = 1000;

struct State {
    bool manual;
    int64_t manualUs;
};

inline State &state() {
    static State s{false, 0};
    return s;
}

inline int64_t wallUs() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration_cast<microseconds>(steady_clock::now() - start).count();
}

inline int64_t nowUs() {
    return state().manual ? state().manualUs : wallUs();
}

inline uint32_t nowMs() {
    return static_cast<uint32_t>(nowUs() / kMicrosPerMilli);
}

inline void useManual(uint32_t startMs) {
    state() = {true, static_cast<int64_t>(startMs) * kMicrosPerMilli};
}

inline void advanceMs(uint32_t ms) {
    state().manualUs += static_cast<int64_t>(ms) * kMicrosPerMilli;
}

inline void sleepMs(uint32_t ms) {
    if (state().manual) {
        advanceMs(ms);
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}
