#pragma once
// lvgl_lock_guard.h — Hold-time recorder for non-UI g_lvglMutex take/give
// pairs (issue #542).
//
// Constructed AFTER a successful `xSemaphoreTake(g_lvglMutex, …) == pdTRUE`,
// the guard latches the take timestamp and on destruction records the
// elapsed microseconds under the matching `MUTEX_HOLD_*` metric. The caller
// is still responsible for `xSemaphoreGive(g_lvglMutex)` — the guard does
// not own the give, because production code already pairs it with the take
// for clarity and our priority is "did we hold the lock too long?", not
// "did we forget to release it?".
//
// When `APP_PROFILE_UI=0` the guard compiles to a zero-cost no-op so
// production builds carry no profiling overhead.

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

    /**
     * Wrap a take/give pair with a hold-time recorder. The guard lives until
     * the enclosing scope ends, which must NOT outlive the matching
     * `xSemaphoreGive(g_lvglMutex)`.
     */
    #define LVGL_HOLD_GUARD(metric) ::LvglLockGuard::Guard _lvgl_hold_guard_##__LINE__(metric)

#else // APP_PROFILE_UI

    /** Profiling disabled — guard is a no-op. */
    #define LVGL_HOLD_GUARD(metric) ((void)0)

#endif // APP_PROFILE_UI
