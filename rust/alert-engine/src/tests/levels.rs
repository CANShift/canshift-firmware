use super::*;

#[test]
fn high_side_under_warn_is_normal() {
    assert_eq!(eval_high_side(50.0, 100.0, 110.0), AlertLevel::Normal);
}

#[test]
fn high_side_between_warn_and_crit_is_warning() {
    assert_eq!(eval_high_side(105.0, 100.0, 110.0), AlertLevel::Warning);
}

#[test]
fn high_side_over_crit_is_critical() {
    assert_eq!(eval_high_side(115.0, 100.0, 110.0), AlertLevel::Critical);
}

#[test]
fn high_side_nan_threshold_is_disabled() {
    assert_eq!(
        eval_high_side(999.0, f32::NAN, f32::NAN),
        AlertLevel::Normal
    );
}

#[test]
fn high_side_only_crit_configured() {
    assert_eq!(eval_high_side(50.0, f32::NAN, 100.0), AlertLevel::Normal);
    assert_eq!(eval_high_side(150.0, f32::NAN, 100.0), AlertLevel::Critical);
}

#[test]
fn high_side_nan_value_is_normal_documented_quirk() {
    assert_eq!(eval_high_side(f32::NAN, 100.0, 110.0), AlertLevel::Normal);
}

#[test]
fn high_side_strict_greater_not_geq() {
    assert_eq!(eval_high_side(100.0, 100.0, 110.0), AlertLevel::Normal);
    assert_eq!(eval_high_side(110.0, 100.0, 110.0), AlertLevel::Warning);
}

#[test]
fn rev_limiter_under_warn_is_normal() {
    assert_eq!(
        eval_rev_limiter(5000.0, 7200.0, 95, 100),
        AlertLevel::Normal
    );
}

#[test]
fn rev_limiter_at_warn_fires_warning() {
    assert_eq!(
        eval_rev_limiter(6840.0, 7200.0, 95, 100),
        AlertLevel::Warning
    );
}

#[test]
fn rev_limiter_at_flash_fires_critical() {
    assert_eq!(
        eval_rev_limiter(7200.0, 7200.0, 95, 100),
        AlertLevel::Critical
    );
}

#[test]
fn rev_limiter_over_flash_stays_critical() {
    assert_eq!(
        eval_rev_limiter(8000.0, 7200.0, 95, 100),
        AlertLevel::Critical
    );
}

fn blink_transitions(window_ms: u32, blink_hz: u8) -> u32 {
    let mut transitions = 0;
    let mut prev = rev_limit_row_lit(0, blink_hz);
    for ms in 1..=window_ms {
        let lit = rev_limit_row_lit(ms, blink_hz);
        if lit != prev {
            transitions += 1;
        }
        prev = lit;
    }
    transitions
}

#[test]
fn rev_limit_row_starts_lit() {
    assert!(rev_limit_row_lit(0, 6));
}

#[test]
fn rev_limit_row_half_period_is_83_ms_at_6_hz() {
    assert!(rev_limit_row_lit(83, 6));
    assert!(!rev_limit_row_lit(84, 6));
    assert!(rev_limit_row_lit(167, 6));
}

#[test]
fn rev_limit_blinks_exactly_6_hz() {
    assert_eq!(blink_transitions(1000, 6), 12);
}

#[test]
fn rev_limit_blink_does_not_drift() {
    assert_eq!(blink_transitions(10_000, 6), 120);
}

#[test]
fn rev_limit_row_stays_lit_when_blinking_is_disabled() {
    assert!(rev_limit_row_lit(0, 0));
    assert!(rev_limit_row_lit(5_000, 0));
}

#[test]
fn coolant_normal_well_below_warn() {
    assert_eq!(
        eval_coolant_temp(80.0, 100.0, 110.0, f32::NAN, f32::NAN),
        AlertLevel::Normal
    );
}

#[test]
fn coolant_caution_within_five_of_warn() {
    assert_eq!(
        eval_coolant_temp(96.0, 100.0, 110.0, f32::NAN, f32::NAN),
        AlertLevel::Caution
    );
}

#[test]
fn coolant_warning_at_warn_threshold() {
    assert_eq!(
        eval_coolant_temp(100.0, 100.0, 110.0, f32::NAN, f32::NAN),
        AlertLevel::Warning
    );
}

#[test]
fn coolant_critical_at_crit_threshold() {
    assert_eq!(
        eval_coolant_temp(110.0, 100.0, 110.0, f32::NAN, f32::NAN),
        AlertLevel::Critical
    );
}

#[test]
fn coolant_high_side_can_escalate_but_not_downgrade() {
    assert_eq!(
        eval_coolant_temp(50.0, 100.0, 110.0, 40.0, 60.0),
        AlertLevel::Warning
    );
    assert_eq!(
        eval_coolant_temp(120.0, 100.0, 110.0, f32::NAN, f32::NAN),
        AlertLevel::Critical
    );
}

#[test]
fn oil_temp_no_caution_band() {
    assert_eq!(
        eval_high_side_temp(119.0, 120.0, 135.0, f32::NAN, f32::NAN),
        AlertLevel::Normal
    );
    assert_eq!(
        eval_high_side_temp(120.0, 120.0, 135.0, f32::NAN, f32::NAN),
        AlertLevel::Warning
    );
}

#[test]
fn oil_pressure_low_side_fires_at_crit() {
    assert_eq!(
        eval_oil_pressure(1.0, 1.5, 1.0, f32::NAN, f32::NAN),
        AlertLevel::Critical
    );
}

#[test]
fn oil_pressure_low_side_warns_at_warn() {
    assert_eq!(
        eval_oil_pressure(1.5, 1.5, 1.0, f32::NAN, f32::NAN),
        AlertLevel::Warning
    );
}

#[test]
fn oil_pressure_high_normal_value() {
    assert_eq!(
        eval_oil_pressure(5.0, 1.5, 1.0, f32::NAN, f32::NAN),
        AlertLevel::Normal
    );
}

#[test]
fn oil_pressure_high_side_escalates_if_configured() {
    assert_eq!(
        eval_oil_pressure(8.0, 1.5, 1.0, 6.0, 7.0),
        AlertLevel::Critical
    );
}

#[test]
fn battery_below_low_crit_is_critical_immediately() {
    assert_eq!(
        eval_battery(10.0, 11.5, 11.0, f32::NAN, f32::NAN),
        AlertLevel::Critical
    );
}

#[test]
fn battery_at_low_crit_is_not_critical_strict_less_than() {
    assert_eq!(
        eval_battery(11.0, 11.5, 11.0, f32::NAN, f32::NAN),
        AlertLevel::Warning
    );
}

#[test]
fn battery_between_low_warn_and_high_warn_is_normal() {
    assert_eq!(
        eval_battery(13.0, 11.5, 11.0, 14.5, 15.0),
        AlertLevel::Normal
    );
}

#[test]
fn battery_high_side_warning() {
    assert_eq!(
        eval_battery(14.8, 11.5, 11.0, 14.5, 15.0),
        AlertLevel::Warning
    );
}

#[test]
fn battery_high_side_critical() {
    assert_eq!(
        eval_battery(15.5, 11.5, 11.0, 14.5, 15.0),
        AlertLevel::Critical
    );
}
