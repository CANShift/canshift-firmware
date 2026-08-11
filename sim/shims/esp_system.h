#pragma once

#include <cstdint>
#include <cstdlib>

inline void esp_restart() {
    exit(0);
}

inline uint32_t esp_random() {
    return static_cast<uint32_t>(rand());
}
