// perf_counters.cpp — UI performance instrumentation (issue #95)

#include "perf_counters.h"

#if APP_PROFILE_UI

    #include <Arduino.h>
    #include <esp_timer.h>
    #include <algorithm>
    #include <string.h>

    #include "diag/logger.h"

namespace {

// Sample reservoir per metric. p95 is computed by sorting a copy of the live
// window — exact for the size below, plenty good enough for an ESP32 dev
// telemetry. Bigger windows would burn DRAM and slow the sort, smaller windows
// would lose tail visibility.
constexpr uint16_t SAMPLES_PER_METRIC = 96;
constexpr uint32_t REPORT_INTERVAL_US = 1000UL * 1000UL; // 1 Hz

// Mutex-wait warning threshold — long lock contention is the smoking gun for
// UI lag. Threshold matches the existing 10 ms xSemaphoreTake() timeout so a
// crossing means the UI task is at risk of skipping its own frame.
constexpr uint32_t PERF_MUTEX_WAIT_WARN_US = 5000;

const char *metricName(PerfCounters::Metric m) {
    switch (m) {
        case PerfCounters::MUTEX_WAIT:
            return "lock";
        case PerfCounters::WIDGETS:
            return "widgets";
        case PerfCounters::TOPBAR:
            return "topbar";
        case PerfCounters::LV_HANDLER:
            return "lvgl";
        case PerfCounters::FRAME_TOTAL:
            return "frame";
        case PerfCounters::PAGE_XSITION:
            return "xsition";
        case PerfCounters::TOUCH_LATENCY:
            return "touch";
        default:
            return "?";
    }
}

struct Window {
    uint32_t samples[SAMPLES_PER_METRIC];
    uint16_t count;
    uint32_t maxUs;
    uint64_t sumUs;

    void clear() {
        count = 0;
        maxUs = 0;
        sumUs = 0;
    }
};

Window s_windows[PerfCounters::METRIC_COUNT];
uint32_t s_frameMissCount = 0;
uint32_t s_flushFrameCount = 0;
bool s_mutexWarnEmittedThisWindow = false;
int64_t s_lastReportUs = 0;

// Touch press → click latency latch.
volatile bool s_touchPressArmed = false;
volatile int64_t s_touchPressUs = 0;

// Page transition start timestamp.
int64_t s_pageXStartUs = 0;

void appendSample(Window &w, uint32_t durationUs) {
    if (w.count < SAMPLES_PER_METRIC) {
        w.samples[w.count++] = durationUs;
    } else {
        // Reservoir-style overwrite when full. Cheap and keeps memory bounded;
        // the 1 Hz tick clears the window before this can run twice for any
        // sane UI load.
        w.samples[w.count % SAMPLES_PER_METRIC] = durationUs;
    }
    w.sumUs += durationUs;
    if (durationUs > w.maxUs) {
        w.maxUs = durationUs;
    }
}

uint32_t computeP95(Window &w) {
    if (w.count == 0)
        return 0;
    uint32_t buf[SAMPLES_PER_METRIC];
    const uint16_t n = (w.count < SAMPLES_PER_METRIC) ? w.count : SAMPLES_PER_METRIC;
    memcpy(buf, w.samples, sizeof(uint32_t) * n);
    std::sort(buf, buf + n);
    // 95th percentile by nearest-rank.
    uint16_t idx = static_cast<uint16_t>((n * 95) / 100);
    if (idx >= n)
        idx = n - 1;
    return buf[idx];
}

} // namespace

namespace PerfCounters {

void init() {
    for (uint8_t i = 0; i < METRIC_COUNT; ++i) {
        s_windows[i].clear();
    }
    s_frameMissCount = 0;
    s_flushFrameCount = 0;
    s_mutexWarnEmittedThisWindow = false;
    s_lastReportUs = esp_timer_get_time();
    s_touchPressArmed = false;
    s_touchPressUs = 0;
    s_pageXStartUs = 0;
    LOG_INFO("PERF", "Profiling enabled (APP_PROFILE_UI=1)");
}

void recordSample(Metric m, uint32_t durationUs) {
    if (m >= METRIC_COUNT)
        return;
    appendSample(s_windows[m], durationUs);
    if (m == MUTEX_WAIT && durationUs > PERF_MUTEX_WAIT_WARN_US && !s_mutexWarnEmittedThisWindow) {
        s_mutexWarnEmittedThisWindow = true;
        LOG_WARN("PERF", "lvgl mutex wait %u µs (>%u)", static_cast<unsigned>(durationUs),
                 static_cast<unsigned>(PERF_MUTEX_WAIT_WARN_US));
    }
}

void recordFlushFrame() {
    ++s_flushFrameCount;
}

void recordFrameMiss() {
    ++s_frameMissCount;
}

void recordTouchPressNow() {
    s_touchPressUs = esp_timer_get_time();
    s_touchPressArmed = true;
}

bool consumeTouchPressTs(uint32_t *outUs) {
    if (!s_touchPressArmed)
        return false;
    const int64_t now = esp_timer_get_time();
    const int64_t delta = now - s_touchPressUs;
    s_touchPressArmed = false;
    if (outUs) {
        *outUs = (delta < 0) ? 0 : static_cast<uint32_t>(delta);
    }
    return true;
}

void recordPageTransitionStart() {
    s_pageXStartUs = esp_timer_get_time();
}

void recordPageTransitionEnd() {
    if (s_pageXStartUs == 0)
        return;
    const int64_t now = esp_timer_get_time();
    const int64_t delta = now - s_pageXStartUs;
    s_pageXStartUs = 0;
    if (delta > 0 && delta < 5000000) { // sanity cap at 5 s
        appendSample(s_windows[PAGE_XSITION], static_cast<uint32_t>(delta));
    }
}

void tick() {
    const int64_t now = esp_timer_get_time();
    if (now - s_lastReportUs < static_cast<int64_t>(REPORT_INTERVAL_US)) {
        return;
    }
    const int64_t windowUs = now - s_lastReportUs;
    s_lastReportUs = now;

    // Build a single line: PERF lock=mean/p95/max widgets=… frame=… fps=N miss=N
    // Keep each metric to 32 chars max to stay under the logger's line budget.
    char buf[256];
    int written = 0;
    for (uint8_t i = 0; i < METRIC_COUNT; ++i) {
        Window &w = s_windows[i];
        if (w.count == 0)
            continue;
        const uint32_t mean = static_cast<uint32_t>(w.sumUs / w.count);
        const uint32_t p95 = computeP95(w);
        const int n = snprintf(buf + written, sizeof(buf) - written, "%s=%u/%u/%u ",
                               metricName(static_cast<Metric>(i)), static_cast<unsigned>(mean),
                               static_cast<unsigned>(p95), static_cast<unsigned>(w.maxUs));
        if (n < 0 || static_cast<size_t>(n) >= sizeof(buf) - written)
            break;
        written += n;
        w.clear();
    }

    // Normalize the FPS count to the actual window duration — vTaskDelay drift
    // and tick() being called slightly late would otherwise undercount.
    const uint32_t fps =
        (windowUs > 0)
            ? static_cast<uint32_t>((static_cast<uint64_t>(s_flushFrameCount) * 1000000ULL) /
                                    static_cast<uint64_t>(windowUs))
            : 0;

    if (written == 0 && s_flushFrameCount == 0 && s_frameMissCount == 0) {
        return;
    }
    LOG_INFO("PERF", "%sfps=%u miss=%u", buf, static_cast<unsigned>(fps),
             static_cast<unsigned>(s_frameMissCount));
    s_frameMissCount = 0;
    s_flushFrameCount = 0;
    s_mutexWarnEmittedThisWindow = false;
}

ScopedTimer::ScopedTimer(Metric m) : m_metric(m), m_startUs(esp_timer_get_time()) {}

ScopedTimer::~ScopedTimer() {
    const int64_t end = esp_timer_get_time();
    const int64_t delta = end - m_startUs;
    if (delta < 0 || delta > 1000000)
        return; // sanity cap (1 s) — discard runaway samples
    appendSample(s_windows[m_metric], static_cast<uint32_t>(delta));
}

} // namespace PerfCounters

#endif // APP_PROFILE_UI
