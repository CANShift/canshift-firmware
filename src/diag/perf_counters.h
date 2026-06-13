#pragma once

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

    MUTEX_HOLD_UI,
    MUTEX_HOLD_USB,
    MUTEX_HOLD_BLE,
    METRIC_COUNT,
};

void init();

void recordSample(Metric m, uint32_t durationUs);

void recordFrameMiss();

void recordTouchPressNow();
bool consumeTouchPressTs(uint32_t *outUs);

void recordPageTransitionStart();
void recordPageTransitionEnd();

void recordFlushFrame();

void tick();

class ScopedTimer {
  public:
    ScopedTimer(Metric m);
    ~ScopedTimer();

  private:
    Metric m_metric;
    int64_t m_startUs;
};

} // namespace PerfCounters

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

#else

    #define PERF_SCOPE(metric) ((void)0)
    #define PERF_RECORD_FRAME_MISS() ((void)0)
    #define PERF_RECORD_TOUCH_PRESS() ((void)0)
    #define PERF_RECORD_PAGE_XSTART() ((void)0)
    #define PERF_RECORD_PAGE_XEND() ((void)0)
    #define PERF_RECORD_FLUSH_FRAME() ((void)0)
    #define PERF_TICK() ((void)0)
    #define PERF_INIT() ((void)0)

#endif
