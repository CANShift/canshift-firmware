
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

uint8_t alert_eval_high_side_rs(float value, float high_warn, float high_crit);

uint8_t alert_eval_rev_limiter_rs(float rpm, float rev_limit_rpm, uint8_t warn_pct,
                                  uint8_t flash_pct);

uint8_t alert_eval_coolant_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                                   float high_crit_c);

uint8_t alert_eval_oil_temp_rs(float temp_c, float warn_c, float crit_c, float high_warn_c,
                               float high_crit_c);

uint8_t alert_eval_oil_pressure_rs(float press_bar, float warn_bar, float crit_bar,
                                   float high_warn_bar, float high_crit_bar);

uint8_t alert_eval_battery_rs(float volts, float low_warn_v, float low_crit_v, float high_warn_v,
                              float high_crit_v);

#ifdef __cplusplus
}
#endif

#endif
