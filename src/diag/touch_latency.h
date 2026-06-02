#pragma once
// touch_latency.h — Production-shipped touch-press → click latency warning.
//
// Always compiled (unlike `PerfCounters::TOUCH_LATENCY`, which is gated behind
// `APP_PROFILE_UI` and therefore absent from release builds). Costs a single
// `esp_timer_get_time` read on the press edge plus a comparison in the click
// handler — sub-microsecond per touch. Issue #1256.
//
// Threshold (`APP_TOUCH_LATENCY_WARN_US` in `app_config.h`) is set above the
// "normal" healthy range so a regression (mutex contention, page rebuild path,
// icon decode) surfaces as a one-line LOG_WARN in field logs instead of as a
// user-reported "feels slow".

#include <stdint.h>

namespace TouchLatency {

// Latch the moment the touch driver sees a fresh press edge.
void recordPressNow();

// Consume the latched timestamp from the next dispatched click and emit a
// LOG_WARN when the elapsed window crosses `APP_TOUCH_LATENCY_WARN_US`.
// No-op (and silent) when no press latch is pending — e.g. the click came
// from a programmatic event, or the latch was already consumed.
void consumePressAndWarnIfSlow();

} // namespace TouchLatency
