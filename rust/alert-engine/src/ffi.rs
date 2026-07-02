use crate::{
    eval_battery, eval_coolant_temp, eval_high_side, eval_oil_pressure, eval_oil_temp,
    eval_rev_limiter, eval_with_hysteresis, sensor_health_step, AlertLevel, LevelHold,
    SensorHealth,
};

const _: () = assert!(core::mem::size_of::<AlertLevel>() == 1);

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

#[cfg(test)]
mod tests {
    use super::*;

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
