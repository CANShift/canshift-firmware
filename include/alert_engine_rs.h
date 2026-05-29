// alert_engine_rs.h — C ABI for the Rust alert-engine crate (issue #1177 R-6).
//
// Hand-written. Six exported functions mirror the anonymous-namespace
// `eval*` helpers in `canshift-firmware/src/runtime/alert_engine.cpp`.
// Each takes the signal value plus the per-signal thresholds as primitives
// — no struct passing across the boundary.
//
// Return value is the `uint8_t` discriminant of `AlertEngine::AlertLevel`:
//   0 = NORMAL, 1 = CAUTION, 2 = WARNING, 3 = CRITICAL.
// The C++ caller does `static_cast<AlertEngine::AlertLevel>(...)` on the
// way back.
//
// All thresholds accept NaN as a "disabled" sentinel — matches the C++
// semantics (`!isnan(threshold) && value > threshold`). A NaN signal value
// returns NORMAL on the high-side comparisons (NaN > x is false). This is
// the C++ original's behaviour, preserved for byte-for-byte parity.
//
// Keep this header in sync with `rust/alert-engine/src/ffi.rs`.

#ifndef CANSHIFT_ALERT_ENGINE_RS_H
#define CANSHIFT_ALERT_ENGINE_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generic NaN-aware high-side check. Returns CRITICAL if value > high_crit
// (and high_crit is finite), WARNING if value > high_warn (and finite),
// else NORMAL. NaN value returns NORMAL.
uint8_t alert_eval_high_side_rs(float value, float high_warn, float high_crit);

// Rev limiter. warn_pct / flash_pct are integer percentages of
// rev_limit_rpm. `>=` semantics on the percent thresholds.
uint8_t alert_eval_rev_limiter_rs(float rpm, float rev_limit_rpm, uint8_t warn_pct,
                                  uint8_t flash_pct);

// Coolant temperature: 4-level chain (NORMAL → CAUTION → WARNING →
// CRITICAL). The CAUTION band is `warn_c - 5.0` to `warn_c` exclusive.
uint8_t alert_eval_coolant_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                                   float high_crit_c);

// Oil temperature: 3-level chain (no CAUTION pre-band).
uint8_t alert_eval_oil_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                               float high_crit_c);

// Oil pressure: LOW side is the primary alert. `<=` semantics on the low
// side. Optional high-side via high_warn_bar / high_crit_bar.
uint8_t alert_eval_oil_pressure_rs(float press_bar, float warn_bar, float crit_bar,
                                   float high_warn_bar, float high_crit_bar);

// Battery voltage: low side is dominant. `<` semantics on the low side.
uint8_t alert_eval_battery_rs(float volts, float low_warn_v, float low_crit_v, float high_warn_v,
                              float high_crit_v);

#ifdef __cplusplus
}
#endif

#endif // CANSHIFT_ALERT_ENGINE_RS_H
