#include "lvgl_tick.h"

#include <esp_timer.h>

namespace {

constexpr int64_t kMicrosPerMilli = 1000;

} // namespace

uint32_t canshift_lv_tick_ms(void) {
    return static_cast<uint32_t>(esp_timer_get_time() / kMicrosPerMilli);
}
