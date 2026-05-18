#pragma once
// perf_counters.h — UI performance instrumentation (issue #95)
//
// Compile-time gated by APP_PROFILE_UI. When 0, every API call collapses to
// a no-op so the production build pays nothing. When 1, the module records
// per-frame timing samples for the UI critical path and emits a single
// structured LOG_INFO("PERF", ...) line at 1 Hz with mean/p95/max for each
// metric.
//
// Counters tracked:
//   MUTEX_WAIT  — xSemaphoreTake(g_lvglMutex) wait time
//   WIDGETS     — WidgetFactory::updateAll duration (inside lock)
//   TOPBAR      — TopBar::update duration (inside lock, when it actually runs)
//   LV_HANDLER  — lv_task_handler() duration (inside lock)
//   FRAME_TOTAL — full taskUI iteration wall time
//   PAGE_XSITION— page transition animation wall time (start → finish_cb)
//   TOUCH_LAT   — press → first LV_EVENT_CLICKED dispatch
//   FRAME_MISS  — count of vTaskDelayUntil overshoots in the current window
//   FPS         — count of completed display refreshes (last flush region) in
//                 the current window; LVGL on-screen overlay also enabled via
//                 LV_USE_PERF_MONITOR when APP_PROFILE_UI=1
//
// Long-wait warnings:
//   recordSample(MUTEX_WAIT, …) emits LOG_WARN("PERF", …) at most once per
//   1 Hz window when the wait exceeds PERF_MUTEX_WAIT_WARN_US — long lock
//   contention is the smoking gun for UI lag.
//
// Usage:
//   #include "diag/perf_counters.h"
//   PERF_SCOPE(PerfCounters::MUTEX_WAIT);   // RAII timing of a block
//   PerfCounters::recordFrameMiss();        // simple counter bump
//   PerfCounters::tick();                   // call once per UI iteration —
//                                           //   emits the 1 Hz summary

#include "app_config.h"

#ifndef APP_PROFILE_UI
    #define APP_PROFILE_UI 0
#endif

#if APP_PROFILE_UI

    #include <stdint.h>

namespace PerfCounters {

enum Metric : uint8_t {
    MUTEX_WAIT = 0,
    WIDGETS,
    TOPBAR,
    LV_HANDLER,
    FRAME_TOTAL,
    PAGE_XSITION,
    TOUCH_LATENCY,
    // Per-holder LVGL mutex hold-time histogram (issue #542). MUTEX_WAIT
    // already tells us when the UI task starved on the lock; these three
    // identify WHO was holding it at the time. Each non-UI take/give pair
    // is wrapped in `LvglLockGuard` from `lvgl_lock_guard.h`.
    MUTEX_HOLD_UI,
    MUTEX_HOLD_USB,
    MUTEX_HOLD_BLE,
    METRIC_COUNT,
};

void init();

// Record a single timing sample (microseconds) for the given metric.
void recordSample(Metric m, uint32_t durationUs);

// Bump the dropped-deadline counter — incremented when vTaskDelayUntil
// overshoots its target period (i.e. the previous frame ran long).
void recordFrameMiss();

// Latch the moment the touch panel reports a fresh press. The next
// LV_EVENT_CLICKED handler closes the loop and feeds the delta to
// recordSample(TOUCH_LATENCY, ...).
void recordTouchPressNow();
bool consumeTouchPressTs(uint32_t *outUs);

// Page-transition timing helpers.
void recordPageTransitionStart();
void recordPageTransitionEnd();

// Bump the per-second FPS counter. Call from the LVGL flush callback only on
// the final region of a refresh (lv_disp_flush_is_last(disp) == true), so the
// counter measures completed frames rather than partial flush regions.
void recordFlushFrame();

// Emit the rolling 1 Hz summary line if it's time to do so. Must be called
// from the UI task once per loop iteration. No-op until 1000 ms elapsed.
void tick();

// Scoped timer — RAII wrapper that records elapsed microseconds on destruction.
class ScopedTimer {
  public:
    ScopedTimer(Metric m);
    ~ScopedTimer();

  private:
    Metric m_metric;
    int64_t m_startUs;
};

} // namespace PerfCounters

    // PERF_SCOPE expands into a unique-named ScopedTimer instance to time
    // the surrounding block. Concatenation with __LINE__ avoids name clashes
    // when several scopes share a function.
    #define PERF_PASTE2(a, b) a##b
    #define PERF_PASTE(a, b) PERF_PASTE2(a, b)
    #define PERF_SCOPE(metric) ::PerfCounters::ScopedTimer PERF_PASTE(_perfScope_, __LINE__)(metric)
    #define PERF_RECORD_FRAME_MISS() ::PerfCounters::recordFrameMiss()
    #define PERF_RECORD_TOUCH_PRESS() ::PerfCounters::recordTouchPressNow()
    #define PERF_RECORD_PAGE_XSTART() ::PerfCounters::recordPageTransitionStart()
    #define PERF_RECORD_PAGE_XEND() ::PerfCounters::recordPageTransitionEnd()
    #define PERF_RECORD_FLUSH_FRAME() ::PerfCounters::recordFlushFrame()
    #define PERF_TICK() ::PerfCounters::tick()
    #define PERF_INIT() ::PerfCounters::init()

#else // APP_PROFILE_UI == 0

    #define PERF_SCOPE(metric) ((void)0)
    #define PERF_RECORD_FRAME_MISS() ((void)0)
    #define PERF_RECORD_TOUCH_PRESS() ((void)0)
    #define PERF_RECORD_PAGE_XSTART() ((void)0)
    #define PERF_RECORD_PAGE_XEND() ((void)0)
    #define PERF_RECORD_FLUSH_FRAME() ((void)0)
    #define PERF_TICK() ((void)0)
    #define PERF_INIT() ((void)0)

#endif // APP_PROFILE_UI
