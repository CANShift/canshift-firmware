
#ifndef CANSHIFT_ALERT_ENGINE_RS_H
#define CANSHIFT_ALERT_ENGINE_RS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
