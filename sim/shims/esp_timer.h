#pragma once

#include <chrono>
#include <cstdint>

inline int64_t esp_timer_get_time() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration_cast<microseconds>(steady_clock::now() - start).count();
}
