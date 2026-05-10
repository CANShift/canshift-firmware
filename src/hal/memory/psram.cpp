// psram.cpp — Runtime PSRAM detection (issue #563).

#include "psram.h"
#include "app_config.h"
#include "diag/logger.h"

#if !APP_SIMULATION_MODE
    #include <Arduino.h>
    #include <esp_heap_caps.h>
#endif

namespace canshift::hal::memory {

namespace {
bool s_initialized = false;
bool s_available = false;
size_t s_totalBytes = 0;
} // namespace

void initPsram() {
    if (s_initialized) {
        return;
    }
    s_initialized = true;

#if !APP_SIMULATION_MODE
    // ESP.getPsramSize() is the canonical Arduino-core probe. On a WROOM
    // chip with -DBOARD_HAS_PSRAM set, the IDF psram init fails silently
    // at boot and this returns 0 — exactly the runtime-detect path we want.
    s_totalBytes = ESP.getPsramSize();
    s_available = s_totalBytes > 0;

    if (s_available) {
        const size_t freeBytes = ESP.getFreePsram();
        LOG_INFO("MEM", "PSRAM detected: %u bytes (free=%u)", static_cast<unsigned>(s_totalBytes),
                 static_cast<unsigned>(freeBytes));
    } else {
        LOG_INFO("MEM", "no PSRAM — using DRAM only");
    }
#else
    s_totalBytes = 0;
    s_available = false;
    LOG_INFO("MEM", "sim build — PSRAM detection skipped");
#endif
}

bool isPsramAvailable() {
    return s_available;
}

size_t getPsramSize() {
    return s_totalBytes;
}

size_t getFreePsram() {
#if !APP_SIMULATION_MODE
    if (!s_available) {
        return 0;
    }
    return ESP.getFreePsram();
#else
    return 0;
#endif
}

} // namespace canshift::hal::memory
