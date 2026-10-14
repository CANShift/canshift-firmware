#pragma once

#include "sim_clock.h"

#include <cstdint>

inline int64_t esp_timer_get_time() {
    return SimClock::nowUs();
}
