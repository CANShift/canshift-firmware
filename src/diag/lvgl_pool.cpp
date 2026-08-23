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

namespace {

constexpr uint8_t kWarnedCap = 4;
const char *s_warned[kWarnedCap] = {nullptr};

// Callers pass string literals, so pointer identity is a stable key. A refused
// surface is retried on every trigger; the warning is worth saying once.
bool warnOnce(const char *surface) {
    for (const char *seen : s_warned) {
        if (seen == surface)
            return false;
    }
    for (const char *&slot : s_warned) {
        if (slot != nullptr)
            continue;
        slot = surface;
        return true;
    }
    return false;
}

} // namespace

bool hasHeadroomFor(uint32_t bytes, const char *surface) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const uint32_t free = static_cast<uint32_t>(mon.free_size);
    if (free >= bytes)
        return true;
    if (warnOnce(surface))
        LOG_WARN("LVGL",
                 "%s not built: needs ~%u B, pool has %u B free — surface stays unavailable",
                 surface, static_cast<unsigned>(bytes), static_cast<unsigned>(free));
    return false;
}

} // namespace LvglPool
