#include "psram.h"
#include "app_config.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

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

    s_totalBytes = ESP.getPsramSize();
    s_available = s_totalBytes > 0;

    if (s_available) {
        const size_t freeBytes = ESP.getFreePsram();
        LOG_INFO("MEM", "PSRAM detected: %u bytes (free=%u)", static_cast<unsigned>(s_totalBytes),
                 static_cast<unsigned>(freeBytes));
    } else {
        LOG_INFO("MEM", "no PSRAM — using DRAM only");
    }
}

bool isPsramAvailable() {
    return s_available;
}

size_t getPsramSize() {
    return s_totalBytes;
}

size_t getFreePsram() {
    if (!s_available) {
        return 0;
    }
    return ESP.getFreePsram();
}

} // namespace canshift::hal::memory
