#include "touch_latency.h"

#include "app_config.h"
#include "diag/logger.h"

#include <esp_timer.h>

namespace TouchLatency {

namespace {

// Both producer + consumer run under g_lvglMutex — no atomics needed.
int64_t s_pressUs = 0;

} // namespace

void recordPressNow() {
    s_pressUs = esp_timer_get_time();
}

void consumePressAndWarnIfSlow() {
    if (s_pressUs == 0)
        return;
    const int64_t deltaUs = esp_timer_get_time() - s_pressUs;
    s_pressUs = 0;
    if (deltaUs < 0)
        return;
    if (static_cast<uint32_t>(deltaUs) >= APP_TOUCH_LATENCY_WARN_US) {
        LOG_WARN("TOUCH", "press→click slow: %u us (threshold %u)", static_cast<unsigned>(deltaUs),
                 static_cast<unsigned>(APP_TOUCH_LATENCY_WARN_US));
    }
}

} // namespace TouchLatency
