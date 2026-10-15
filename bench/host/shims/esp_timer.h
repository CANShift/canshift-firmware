#pragma once

#include "bench_clock.h"

#include <cstdint>

inline int64_t esp_timer_get_time() {
    return BenchClock::nowUs();
}
