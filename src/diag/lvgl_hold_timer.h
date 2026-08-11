#pragma once

#include "app_config.h"
#include "diag/perf_counters.h"

#if APP_PROFILE_UI

    #include <esp_timer.h>

class LvglHoldTimer {
  public:
    explicit LvglHoldTimer(PerfCounters::Metric holder)
        : m_holder(holder), m_startUs(esp_timer_get_time()) {}
    ~LvglHoldTimer() {
        const int64_t deltaUs = esp_timer_get_time() - m_startUs;
        if (deltaUs > 0) {
            PerfCounters::recordSample(m_holder, static_cast<uint32_t>(deltaUs));
        }
    }

    LvglHoldTimer(const LvglHoldTimer &) = delete;
    LvglHoldTimer &operator=(const LvglHoldTimer &) = delete;
    LvglHoldTimer(LvglHoldTimer &&) = delete;
    LvglHoldTimer &operator=(LvglHoldTimer &&) = delete;

  private:
    PerfCounters::Metric m_holder;
    int64_t m_startUs;
};

    #define LVGL_HOLD_TIMER(metric) ::LvglHoldTimer _lvgl_hold_timer_##__LINE__(metric)

#else

    #define LVGL_HOLD_TIMER(metric) ((void)0)

#endif
