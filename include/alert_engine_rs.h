
#ifndef CANSHIFT_ALERT_ENGINE_RS_H
#define CANSHIFT_ALERT_ENGINE_RS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Layout mirror of rust/alert-engine SensorHealth (repr(C)) — keep in sync. */
typedef struct {
    bool ever_valid;
    bool lost;
    bool prev_valid;
    uint32_t valid_since_ms;
} AlertSensorHealthRs;

void alert_sensor_health_step_rs(AlertSensorHealthRs *health, bool valid, uint32_t now_ms,
                                 uint32_t clear_hold_ms);

/* Layout mirror of rust/alert-engine LevelHold (repr(C)) — keep in sync. */
typedef struct {
    uint8_t level;
    uint32_t raised_at_ms;
} AlertLevelHoldRs;

uint8_t alert_coolant_temp_step_rs(AlertLevelHoldRs *hold, float temp_c, float warn_c, float crit_c,
                                   float high_warn_c, float high_crit_c, uint32_t now_ms,
                                   float hysteresis_pct, uint32_t min_active_ms);

uint8_t alert_oil_temp_step_rs(AlertLevelHoldRs *hold, float temp_c, float warn_c, float crit_c,
                               float high_warn_c, float high_crit_c, uint32_t now_ms,
                               float hysteresis_pct, uint32_t min_active_ms);

uint8_t alert_oil_pressure_step_rs(AlertLevelHoldRs *hold, float press_bar, float warn_bar,
                                   float crit_bar, float high_warn_bar, float high_crit_bar,
                                   uint32_t now_ms, float hysteresis_pct, uint32_t min_active_ms);

uint8_t alert_battery_step_rs(AlertLevelHoldRs *hold, float volts, float low_warn_v,
                              float low_crit_v, float high_warn_v, float high_crit_v,
                              uint32_t now_ms, float hysteresis_pct, uint32_t min_active_ms);

/* Mirror of rust/alert-engine SEVERITY_LEVEL_COUNT — gated by check_ffi_parity.py. */
enum { ALERT_SEVERITY_LEVEL_COUNT = 4 };

/* Mirror of rust/alert-engine Severity (repr(u8)) — keep in sync. */
enum {
    ALERT_SEVERITY_INFORMATION = 0,
    ALERT_SEVERITY_WARNING = 1,
    ALERT_SEVERITY_CRITICAL = 2,
    ALERT_SEVERITY_FAILURE = 3
};

float alert_warn_level_for_rs(float danger_level, bool danger_below, float sig_warn,
                              float sig_high_warn);

uint8_t alert_severity_for_reading_rs(float value, float warn_level, float danger_level,
                                      bool danger_below);

/* Layout mirror of rust/alert-engine CrossedLimit (repr(C)) — keep in sync. */
typedef struct {
    float limit;
    bool below;
    bool valid;
} AlertCrossedLimitRs;

void alert_crossed_limit_rs(AlertCrossedLimitRs *out, float value, float primary,
                            bool primary_below, float high_crit);

uint8_t alert_eval_high_side_rs(float value, float high_warn, float high_crit);

uint8_t alert_eval_rev_limiter_rs(float rpm, float rev_limit_rpm, uint8_t warn_pct,
                                  uint8_t flash_pct);

bool alert_rev_limit_row_lit_rs(uint32_t elapsed_ms, uint8_t blink_hz);

uint8_t alert_eval_coolant_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                                   float high_crit_c);

uint8_t alert_eval_oil_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                               float high_crit_c);

uint8_t alert_eval_oil_pressure_rs(float press_bar, float warn_bar, float crit_bar,
                                   float high_warn_bar, float high_crit_bar);

uint8_t alert_eval_battery_rs(float volts, float low_warn_v, float low_crit_v, float high_warn_v,
                              float high_crit_v);

/* Layout mirror of rust/alert-engine BusSilence (repr(C)) — keep in sync. */
typedef struct {
    bool silent;
    uint32_t seconds;
} AlertBusSilenceRs;

/* Mirror of rust/alert-engine STALE_DASH_GROUPS_MAX — gated by check_ffi_parity.py. */
enum { ALERT_STALE_DASH_GROUPS_MAX = 4 };

void alert_bus_silence_rs(AlertBusSilenceRs *out, uint32_t ms_since_rx, uint32_t uptime_ms,
                          uint32_t threshold_ms);

uint8_t alert_stale_dash_groups_rs(float max_value);

#ifdef __cplusplus
}
#endif

#endif
