// touch_latency.cpp — see touch_latency.h.

#include "touch_latency.h"

#include "app_config.h"
#include "diag/logger.h"

#include <esp_timer.h>

namespace TouchLatency {

namespace {

// Single-slot latch. Zero = no press pending. Reads/writes happen from the
// LVGL task (touch driver press edge + global click event cb), so no atomics
// needed — both run cooperatively under `g_lvglMutex`.
int64_t s_pressUs = 0;

} // namespace

void recordPressNow() {
    s_pressUs = esp_timer_get_time();
}

void consumePressAndWarnIfSlow() {
    if (s_pressUs == 0)
        return; // No press latched (programmatic click, already consumed, etc.).
    const int64_t deltaUs = esp_timer_get_time() - s_pressUs;
    s_pressUs = 0;
    if (deltaUs < 0)
        return; // Clock anomaly — drop silently.
    if (static_cast<uint32_t>(deltaUs) >= APP_TOUCH_LATENCY_WARN_US) {
        LOG_WARN("TOUCH", "press→click slow: %u us (threshold %u)", static_cast<unsigned>(deltaUs),
                 static_cast<unsigned>(APP_TOUCH_LATENCY_WARN_US));
    }
}

} // namespace TouchLatency
