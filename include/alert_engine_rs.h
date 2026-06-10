// Returns AlertEngine::AlertLevel: 0=NORMAL, 1=CAUTION, 2=WARNING, 3=CRITICAL.
// NaN threshold = disabled; NaN value → NORMAL on high-side compares.
// Keep in sync with rust/alert-engine/src/ffi.rs (#1177 R-6).
#ifndef CANSHIFT_ALERT_ENGINE_RS_H
#define CANSHIFT_ALERT_ENGINE_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t alert_eval_high_side_rs(float value, float high_warn, float high_crit);

uint8_t alert_eval_rev_limiter_rs(float rpm, float rev_limit_rpm, uint8_t warn_pct,
                                  uint8_t flash_pct);

// 4-level chain — CAUTION band is `warn_c - 5.0` to `warn_c` exclusive.
uint8_t alert_eval_coolant_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                                   float high_crit_c);

// 3-level chain (no CAUTION pre-band).
uint8_t alert_eval_oil_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                               float high_crit_c);

// Low side primary; <= semantics.
uint8_t alert_eval_oil_pressure_rs(float press_bar, float warn_bar, float crit_bar,
                                   float high_warn_bar, float high_crit_bar);

// Low side dominant; < semantics.
uint8_t alert_eval_battery_rs(float volts, float low_warn_v, float low_crit_v, float high_warn_v,
                              float high_crit_v);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_ALERT_ENGINE_RS_H
