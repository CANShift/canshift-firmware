#include "diag/lvgl_pool.h"

#include "diag/logger.h"
#include "lvgl_pool_alloc.h"

#include <lvgl.h>

namespace LvglPool {

namespace {

constexpr uint32_t kPercent = 100U;

#if LV_MEM_CUSTOM
Stats readStats() {
    CanshiftLvglPoolInfo info = {};
    if (!canshift_lvgl_pool_info(&info))
        return {};
    return {static_cast<uint32_t>(info.total), static_cast<uint32_t>(info.free),
            static_cast<uint32_t>(info.largest)};
}
#else
Stats readStats() {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    return {static_cast<uint32_t>(mon.total_size), static_cast<uint32_t>(mon.free_size),
            static_cast<uint32_t>(mon.free_biggest_size)};
}
#endif

uint32_t fragmentationPct(const Stats &s) {
    if (s.free == 0)
        return 0;
    return kPercent - (s.largest * kPercent) / s.free;
}

}

Stats stats() {
    return readStats();
}

void report(const char *phase) {
    const Stats s = stats();
    const uint32_t used = s.total > s.free ? s.total - s.free : 0;
    const uint32_t usedPct = s.total > 0 ? (used * kPercent) / s.total : 0;
    LOG_INFO("LVGL", "pool after %s: used=%u/%u B (%u%%) frag=%u%% largest=%u B", phase,
             static_cast<unsigned>(used), static_cast<unsigned>(s.total),
             static_cast<unsigned>(usedPct), static_cast<unsigned>(fragmentationPct(s)),
             static_cast<unsigned>(s.largest));
}

}
