use super::*;

#[test]
fn severity_high_side_walks_the_three_tiers() {
    assert_eq!(
        severity_for_reading(88.0, 110.0, 125.0, false),
        Severity::Information
    );
    assert_eq!(
        severity_for_reading(128.0, 110.0, 125.0, false),
        Severity::Failure
    );
    assert_eq!(
        severity_for_reading(112.0, 110.0, 125.0, false),
        Severity::Warning
    );
}

#[test]
fn severity_low_side_walks_the_three_tiers() {
    assert_eq!(
        severity_for_reading(4.1, 2.0, 1.0, true),
        Severity::Information
    );
    assert_eq!(severity_for_reading(1.8, 2.0, 1.0, true), Severity::Warning);
    assert_eq!(severity_for_reading(0.4, 2.0, 1.0, true), Severity::Failure);
}

#[test]
fn severity_nan_threshold_disables_its_tier() {
    assert_eq!(
        severity_for_reading(300.0, f32::NAN, f32::NAN, false),
        Severity::Information
    );
    assert_eq!(
        severity_for_reading(300.0, f32::NAN, 125.0, false),
        Severity::Failure
    );
    assert_eq!(
        severity_for_reading(f32::NAN, 110.0, 125.0, false),
        Severity::Information
    );
}

#[test]
fn signal_severity_reads_both_sides_of_a_battery() {
    let at = |v: f32| severity_for_signal(v, 12.0, 11.5, 15.0, 16.0);
    assert_eq!(at(13.8), Severity::Information);
    assert_eq!(at(11.9), Severity::Warning);
    assert_eq!(at(10.8), Severity::Failure);
    assert_eq!(at(15.2), Severity::Warning);
    assert_eq!(at(16.1), Severity::Failure);
}

#[test]
fn signal_severity_infers_the_primary_side_from_the_order() {
    let oil = |v: f32| severity_for_signal(v, 1.5, 1.0, f32::NAN, f32::NAN);
    assert_eq!(oil(3.0), Severity::Information);
    assert_eq!(oil(1.4), Severity::Warning);
    assert_eq!(oil(0.9), Severity::Failure);
    let coolant = |v: f32| severity_for_signal(v, 100.0, 115.0, f32::NAN, f32::NAN);
    assert_eq!(coolant(105.0), Severity::Warning);
    assert_eq!(coolant(116.0), Severity::Failure);
}

#[test]
fn signal_severity_without_levels_stays_quiet() {
    let none = f32::NAN;
    assert_eq!(
        severity_for_signal(9999.0, none, none, none, none),
        Severity::Information
    );
    assert_eq!(
        severity_for_signal(120.0, none, 115.0, none, none),
        Severity::Failure
    );
}

#[test]
fn severity_never_returns_the_takeover_level() {
    assert_ne!(
        severity_for_reading(0.0, 2.0, 1.0, true),
        Severity::Critical
    );
}

#[test]
fn max_level_picks_more_severe() {
    assert_eq!(
        AlertLevel::Warning.max(AlertLevel::Normal),
        AlertLevel::Warning
    );
    assert_eq!(
        AlertLevel::Critical.max(AlertLevel::Warning),
        AlertLevel::Critical
    );
    assert_eq!(
        AlertLevel::Caution.max(AlertLevel::Caution),
        AlertLevel::Caution
    );
}

#[test]
fn crossed_limit_reports_the_low_side_threshold() {
    let c = crossed_limit(0.4, 1.0, true, f32::NAN);
    assert!(c.valid);
    assert!(c.below);
    assert_eq!(c.limit, 1.0);
}

#[test]
fn crossed_limit_reports_the_high_side_threshold() {
    let c = crossed_limit(128.0, 125.0, false, f32::NAN);
    assert!(c.valid);
    assert!(!c.below);
    assert_eq!(c.limit, 125.0);
}

#[test]
fn crossed_limit_prefers_the_high_crit_when_both_are_crossed() {
    let c = crossed_limit(16.5, 11.5, true, 15.5);
    assert!(c.valid);
    assert!(!c.below);
    assert_eq!(c.limit, 15.5);
}

#[test]
fn crossed_limit_is_invalid_when_nothing_is_crossed() {
    assert!(!crossed_limit(4.1, 1.0, true, f32::NAN).valid);
}

#[test]
fn display_idle_walks_live_dim_off() {
    assert_eq!(display_idle_state(0, 60_000, 600_000), DISPLAY_IDLE_LIVE);
    assert_eq!(
        display_idle_state(59_999, 60_000, 600_000),
        DISPLAY_IDLE_LIVE
    );
    assert_eq!(
        display_idle_state(60_000, 60_000, 600_000),
        DISPLAY_IDLE_DIM
    );
    assert_eq!(
        display_idle_state(599_999, 60_000, 600_000),
        DISPLAY_IDLE_DIM
    );
    assert_eq!(
        display_idle_state(600_000, 60_000, 600_000),
        DISPLAY_IDLE_OFF
    );
}

#[test]
fn display_idle_zero_threshold_disables_that_step() {
    assert_eq!(display_idle_state(u32::MAX, 0, 600_000), DISPLAY_IDLE_OFF);
    assert_eq!(display_idle_state(u32::MAX, 60_000, 0), DISPLAY_IDLE_DIM);
    assert_eq!(display_idle_state(u32::MAX, 0, 0), DISPLAY_IDLE_LIVE);
}

#[test]
fn display_idle_off_wins_when_thresholds_are_inverted() {
    assert_eq!(display_idle_state(10_000, 600_000, 5_000), DISPLAY_IDLE_OFF);
}

#[test]
fn crossed_limit_ignores_nan_thresholds() {
    assert!(!crossed_limit(0.4, f32::NAN, true, f32::NAN).valid);
}
