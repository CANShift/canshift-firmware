use super::*;

#[test]
fn ffi_signal_severity_passes_through() {
    assert_eq!(
        alert_severity_for_signal_rs(10.8, 12.0, 11.5, 15.0, 16.0),
        3
    );
    assert_eq!(
        alert_severity_for_signal_rs(13.8, 12.0, 11.5, 15.0, 16.0),
        0
    );
}

#[test]
fn ffi_crossed_limit_null_is_noop() {
    unsafe { alert_crossed_limit_rs(core::ptr::null_mut(), 0.4, 1.0, true, f32::NAN) };
}

#[test]
fn ffi_crossed_limit_writes_the_low_side_threshold() {
    let mut out = CrossedLimit::default();
    unsafe { alert_crossed_limit_rs(&mut out, 0.4, 1.0, true, f32::NAN) };
    assert!(out.valid);
    assert!(out.below);
    assert_eq!(out.limit, 1.0);
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

fn coolant_step(hold: &mut LevelHold, temp_c: f32, now_ms: u32) -> u8 {
    unsafe {
        alert_coolant_temp_step_rs(
            hold,
            temp_c,
            100.0,
            110.0,
            f32::NAN,
            f32::NAN,
            now_ms,
            2.0,
            2000,
        )
    }
}

#[test]
fn ffi_hold_steps_in_place_and_returns_level() {
    let mut h = LevelHold::default();
    assert_eq!(coolant_step(&mut h, 120.0, 0), 3);
    assert_eq!(coolant_step(&mut h, 80.0, 500), 3);
    assert_eq!(coolant_step(&mut h, 80.0, 2000), 0);
    assert_eq!(h.level, AlertLevel::Normal);
}
