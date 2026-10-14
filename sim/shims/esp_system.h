#pragma once

#include "sim_random.h"

#include <cstdint>
#include <cstdlib>

namespace SimRestart {

using Hook = void (*)();

inline Hook &hook() {
    static Hook installed = nullptr;
    return installed;
}

} // namespace SimRestart

inline void esp_restart() {
    if (SimRestart::hook() == nullptr)
        exit(0);
    SimRestart::hook()();
}

inline uint32_t esp_random() {
    return SimRandom::next();
}
