use crate::{
    eval_battery, eval_coolant_temp, eval_high_side, eval_oil_pressure, eval_oil_temp,
    eval_rev_limiter, sensor_health_step, AlertLevel, SensorHealth,
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
        assert_eq!(alert_eval_battery_rs(10.0, 11.5, 11.0, f32::NAN, f32::NAN), 3);
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
}
