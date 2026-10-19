use crate::bus_silence::{bus_silence, stale_dash_groups, BusSilence};
use crate::critical_alert::eval_critical_alert;
use crate::{
    crossed_limit, eval_battery, eval_coolant_temp, eval_high_side, eval_high_side_temp,
    eval_oil_pressure, eval_rev_limiter, eval_with_hysteresis, rev_limit_row_lit,
    sensor_health_step, severity_for_reading, severity_for_signal, AlertLevel, CrossedLimit,
    LevelHold, SensorHealth, Severity, SEVERITY_LEVEL_COUNT,
};

const _: () = assert!(core::mem::size_of::<AlertLevel>() == 1);
const _: () = assert!(core::mem::size_of::<Severity>() == 1);
const _: () = assert!(Severity::Failure as u8 == SEVERITY_LEVEL_COUNT - 1);
const _: () = assert!(core::mem::size_of::<BusSilence>() == 8);
const _: () = assert!(core::mem::align_of::<BusSilence>() == 4);

#[no_mangle]
pub unsafe extern "C" fn alert_bus_silence_rs(
    out: *mut BusSilence,
    ms_since_rx: u32,
    uptime_ms: u32,
    threshold_ms: u32,
) {
    let Some(slot) = out.as_mut() else {
        return;
    };
    *slot = bus_silence(ms_since_rx, uptime_ms, threshold_ms);
}

#[no_mangle]
pub extern "C" fn alert_stale_dash_groups_rs(max_value: f32) -> u8 {
    stale_dash_groups(max_value)
}

#[no_mangle]
pub extern "C" fn alert_eval_high_side_rs(value: f32, high_warn: f32, high_crit: f32) -> u8 {
    eval_high_side(value, high_warn, high_crit) as u8
}

#[no_mangle]
pub extern "C" fn alert_eval_rev_limiter_rs(
    rpm: f32,
    rev_limit_rpm: f32,
    warn_pct: u8,
    flash_pct: u8,
) -> u8 {
    eval_rev_limiter(rpm, rev_limit_rpm, warn_pct, flash_pct) as u8
}

#[no_mangle]
pub extern "C" fn alert_rev_limit_row_lit_rs(elapsed_ms: u32, blink_hz: u8) -> bool {
    rev_limit_row_lit(elapsed_ms, blink_hz)
}

#[no_mangle]
pub extern "C" fn alert_eval_coolant_temp_rs(
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
) -> u8 {
    eval_coolant_temp(temp_c, warn_c, crit_c, high_warn_c, high_crit_c) as u8
}

#[no_mangle]
pub extern "C" fn alert_eval_oil_temp_rs(
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
) -> u8 {
    eval_high_side_temp(temp_c, warn_c, crit_c, high_warn_c, high_crit_c) as u8
}

#[no_mangle]
pub extern "C" fn alert_eval_oil_pressure_rs(
    press_bar: f32,
    warn_bar: f32,
    crit_bar: f32,
    high_warn_bar: f32,
    high_crit_bar: f32,
) -> u8 {
    eval_oil_pressure(press_bar, warn_bar, crit_bar, high_warn_bar, high_crit_bar) as u8
}

#[no_mangle]
pub extern "C" fn alert_eval_battery_rs(
    volts: f32,
    low_warn_v: f32,
    low_crit_v: f32,
    high_warn_v: f32,
    high_crit_v: f32,
) -> u8 {
    eval_battery(volts, low_warn_v, low_crit_v, high_warn_v, high_crit_v) as u8
}

#[no_mangle]
pub unsafe extern "C" fn alert_sensor_health_step_rs(
    health: *mut SensorHealth,
    valid: bool,
    now_ms: u32,
    clear_hold_ms: u32,
) {
    let Some(h) = health.as_mut() else {
        return;
    };
    *h = sensor_health_step(*h, valid, now_ms, clear_hold_ms);
}

unsafe fn step_hold(
    hold: *mut LevelHold,
    value: f32,
    now_ms: u32,
    hysteresis_pct: f32,
    min_active_ms: u32,
    eval: impl Fn(f32) -> AlertLevel,
) -> u8 {
    let Some(h) = hold.as_mut() else {
        return AlertLevel::Normal as u8;
    };
    *h = eval_with_hysteresis(*h, value, now_ms, hysteresis_pct, min_active_ms, eval);
    h.level as u8
}

#[no_mangle]
pub extern "C" fn alert_display_idle_state_rs(
    idle_ms: u32,
    dim_after_ms: u32,
    off_after_ms: u32,
) -> u8 {
    crate::display_idle_state(idle_ms, dim_after_ms, off_after_ms)
}

#[no_mangle]
pub unsafe extern "C" fn alert_critical_alert_step_rs(
    hold: *mut LevelHold,
    value: f32,
    fires_at: f32,
    below: bool,
    now_ms: u32,
    hysteresis_pct: f32,
    min_active_ms: u32,
) -> u8 {
    step_hold(hold, value, now_ms, hysteresis_pct, min_active_ms, |v| {
        eval_critical_alert(v, fires_at, below)
    })
}

#[no_mangle]
pub unsafe extern "C" fn alert_coolant_temp_step_rs(
    hold: *mut LevelHold,
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
    now_ms: u32,
    hysteresis_pct: f32,
    min_active_ms: u32,
) -> u8 {
    step_hold(hold, temp_c, now_ms, hysteresis_pct, min_active_ms, |v| {
        eval_coolant_temp(v, warn_c, crit_c, high_warn_c, high_crit_c)
    })
}

#[no_mangle]
pub unsafe extern "C" fn alert_high_side_temp_step_rs(
    hold: *mut LevelHold,
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
    now_ms: u32,
    hysteresis_pct: f32,
    min_active_ms: u32,
) -> u8 {
    step_hold(hold, temp_c, now_ms, hysteresis_pct, min_active_ms, |v| {
        eval_high_side_temp(v, warn_c, crit_c, high_warn_c, high_crit_c)
    })
}

#[no_mangle]
pub unsafe extern "C" fn alert_oil_pressure_step_rs(
    hold: *mut LevelHold,
    press_bar: f32,
    warn_bar: f32,
    crit_bar: f32,
    high_warn_bar: f32,
    high_crit_bar: f32,
    now_ms: u32,
    hysteresis_pct: f32,
    min_active_ms: u32,
) -> u8 {
    step_hold(
        hold,
        press_bar,
        now_ms,
        hysteresis_pct,
        min_active_ms,
        |v| eval_oil_pressure(v, warn_bar, crit_bar, high_warn_bar, high_crit_bar),
    )
}

#[no_mangle]
pub unsafe extern "C" fn alert_battery_step_rs(
    hold: *mut LevelHold,
    volts: f32,
    low_warn_v: f32,
    low_crit_v: f32,
    high_warn_v: f32,
    high_crit_v: f32,
    now_ms: u32,
    hysteresis_pct: f32,
    min_active_ms: u32,
) -> u8 {
    step_hold(hold, volts, now_ms, hysteresis_pct, min_active_ms, |v| {
        eval_battery(v, low_warn_v, low_crit_v, high_warn_v, high_crit_v)
    })
}

#[no_mangle]
pub extern "C" fn alert_severity_for_reading_rs(
    value: f32,
    warn_level: f32,
    danger_level: f32,
    danger_below: bool,
) -> u8 {
    severity_for_reading(value, warn_level, danger_level, danger_below) as u8
}

#[no_mangle]
pub unsafe extern "C" fn alert_crossed_limit_rs(
    out: *mut CrossedLimit,
    value: f32,
    primary: f32,
    primary_below: bool,
    high_crit: f32,
) {
    let Some(out) = out.as_mut() else {
        return;
    };
    *out = crossed_limit(value, primary, primary_below, high_crit);
}

#[no_mangle]
pub extern "C" fn alert_severity_for_signal_rs(
    value: f32,
    warn_level: f32,
    danger_level: f32,
    high_warn_level: f32,
    high_danger_level: f32,
) -> u8 {
    severity_for_signal(
        value,
        warn_level,
        danger_level,
        high_warn_level,
        high_danger_level,
    ) as u8
}

#[cfg(test)]
mod tests;
