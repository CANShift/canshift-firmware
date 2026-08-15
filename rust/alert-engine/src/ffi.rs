use crate::bus_silence::{bus_silence, stale_dash_groups, BusSilence};
use crate::{
    eval_battery, eval_coolant_temp, eval_high_side, eval_oil_pressure, eval_oil_temp,
    eval_rev_limiter, eval_with_hysteresis, rev_limit_row_lit, sensor_health_step,
    severity_for_reading, warn_level_for, AlertLevel, LevelHold, SensorHealth, Severity,
    SEVERITY_LEVEL_COUNT,
};

const _: () = assert!(core::mem::size_of::<AlertLevel>() == 1);
const _: () = assert!(core::mem::size_of::<Severity>() == 1);
const _: () = assert!(Severity::Failure as u8 == SEVERITY_LEVEL_COUNT - 1);
const _: () = assert!(core::mem::size_of::<BusSilence>() == 8);
const _: () = assert!(core::mem::align_of::<BusSilence>() == 4);

/// # Safety
/// `out` must point to a valid, writable `BusSilence` (the C++ mirror struct
/// `AlertBusSilenceRs` in alert_engine_rs.h — layouts must stay in sync).
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
    eval_oil_temp(temp_c, warn_c, crit_c, high_warn_c, high_crit_c) as u8
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

/// # Safety
/// `health` must point to a valid, writable `SensorHealth` (the C++ mirror
/// struct in alert_engine_rs.h — layouts must stay in sync).
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

/// # Safety
/// `hold` must point to a valid, writable `LevelHold` (the C++ mirror struct
/// in alert_engine_rs.h — layouts must stay in sync). Applies to all
/// `alert_*_step_rs` functions below.
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

/// # Safety
/// See `step_hold`.
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

/// # Safety
/// See `step_hold`.
#[no_mangle]
pub unsafe extern "C" fn alert_oil_temp_step_rs(
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
        eval_oil_temp(v, warn_c, crit_c, high_warn_c, high_crit_c)
    })
}

/// # Safety
/// See `step_hold`.
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

/// # Safety
/// See `step_hold`.
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
pub extern "C" fn alert_warn_level_for_rs(
    danger_level: f32,
    danger_below: bool,
    sig_warn: f32,
    sig_high_warn: f32,
) -> f32 {
    warn_level_for(danger_level, danger_below, sig_warn, sig_high_warn)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_warn_level_passes_through() {
        assert_eq!(
            alert_warn_level_for_rs(150.0, false, 130.0, f32::NAN),
            130.0
        );
        assert!(alert_warn_level_for_rs(240.0, false, 250.0, f32::NAN).is_nan());
    }

    #[test]
    fn ffi_severity_maps_the_four_levels_to_their_discriminants() {
        assert_eq!(alert_severity_for_reading_rs(88.0, 110.0, 125.0, false), 0);
        assert_eq!(alert_severity_for_reading_rs(112.0, 110.0, 125.0, false), 1);
        assert_eq!(alert_severity_for_reading_rs(128.0, 110.0, 125.0, false), 3);
    }

    #[test]
    fn ffi_returns_normal_as_zero() {
        assert_eq!(alert_eval_high_side_rs(50.0, 100.0, 110.0), 0);
    }

    #[test]
    fn ffi_returns_critical_as_three() {
        assert_eq!(alert_eval_high_side_rs(200.0, 100.0, 110.0), 3);
    }

    #[test]
    fn ffi_battery_low_critical_path() {
        assert_eq!(
            alert_eval_battery_rs(10.0, 11.5, 11.0, f32::NAN, f32::NAN),
            3
        );
    }

    #[test]
    fn ffi_rev_limiter_at_flash_is_critical() {
        assert_eq!(alert_eval_rev_limiter_rs(7200.0, 7200.0, 95, 100), 3);
    }

    #[test]
    fn ffi_rev_limit_row_toggles_at_the_half_period() {
        assert!(alert_rev_limit_row_lit_rs(0, 6));
        assert!(!alert_rev_limit_row_lit_rs(84, 6));
        assert!(alert_rev_limit_row_lit_rs(167, 6));
    }

    #[test]
    fn ffi_coolant_caution_band() {
        assert_eq!(
            alert_eval_coolant_temp_rs(97.0, 100.0, 110.0, f32::NAN, f32::NAN),
            1
        );
    }

    #[test]
    fn ffi_sensor_health_null_is_noop() {
        unsafe { alert_sensor_health_step_rs(core::ptr::null_mut(), true, 0, 3000) };
    }

    #[test]
    fn ffi_sensor_health_steps_in_place() {
        let mut h = SensorHealth::default();
        unsafe {
            alert_sensor_health_step_rs(&mut h, true, 0, 3000);
            alert_sensor_health_step_rs(&mut h, false, 2000, 3000);
        }
        assert!(h.lost);
    }

    #[test]
    fn ffi_hold_null_is_noop_normal() {
        let level = unsafe {
            alert_coolant_temp_step_rs(
                core::ptr::null_mut(),
                120.0,
                100.0,
                110.0,
                f32::NAN,
                f32::NAN,
                0,
                2.0,
                2000,
            )
        };
        assert_eq!(level, 0);
    }

    #[test]
    fn ffi_hold_steps_in_place_and_returns_level() {
        let mut h = LevelHold::default();
        let level = unsafe {
            alert_coolant_temp_step_rs(
                &mut h,
                120.0,
                100.0,
                110.0,
                f32::NAN,
                f32::NAN,
                0,
                2.0,
                2000,
            )
        };
        assert_eq!(level, 3);
        let level = unsafe {
            alert_coolant_temp_step_rs(
                &mut h,
                80.0,
                100.0,
                110.0,
                f32::NAN,
                f32::NAN,
                500,
                2.0,
                2000,
            )
        };
        assert_eq!(level, 3);
        let level = unsafe {
            alert_coolant_temp_step_rs(
                &mut h,
                80.0,
                100.0,
                110.0,
                f32::NAN,
                f32::NAN,
                2000,
                2.0,
                2000,
            )
        };
        assert_eq!(level, 0);
        assert_eq!(h.level, AlertLevel::Normal);
    }
}
