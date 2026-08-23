#include "diag/lvgl_pool.h"

#include "diag/logger.h"

#include <lvgl.h>

namespace LvglPool {

void report(const char *phase) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const uint32_t total = static_cast<uint32_t>(mon.total_size);
    const uint32_t free = static_cast<uint32_t>(mon.free_size);
    const uint32_t used = total > free ? total - free : 0;
    const uint32_t usedPct = total > 0 ? (used * 100U) / total : 0;
    LOG_INFO("LVGL", "pool after %s: used=%u/%u B (%u%%) frag=%u%% largest=%u B", phase,
             static_cast<unsigned>(used), static_cast<unsigned>(total),
             static_cast<unsigned>(usedPct), static_cast<unsigned>(mon.frag_pct),
             static_cast<unsigned>(mon.free_biggest_size));
}

} // namespace LvglPool
