#pragma once

#include "app_config.h"
#include "diag/perf_counters.h"

#if APP_PROFILE_UI

    #include <esp_timer.h>

namespace LvglLockGuard {

class Guard {
  public:
    explicit Guard(PerfCounters::Metric holder)
        : m_holder(holder), m_startUs(esp_timer_get_time()) {}
    ~Guard() {
        const int64_t deltaUs = esp_timer_get_time() - m_startUs;
        if (deltaUs > 0) {
            PerfCounters::recordSample(m_holder, static_cast<uint32_t>(deltaUs));
        }
    }

    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
    Guard(Guard &&) = delete;
    Guard &operator=(Guard &&) = delete;

  private:
    PerfCounters::Metric m_holder;
    int64_t m_startUs;
};

} // namespace LvglLockGuard

    #define LVGL_HOLD_GUARD(metric) ::LvglLockGuard::Guard _lvgl_hold_guard_##__LINE__(metric)

#else

    #define LVGL_HOLD_GUARD(metric) ((void)0)

#endif
