use super::*;

const HOLD_MS: u32 = 3000;

fn run(states: &[(bool, u32)]) -> SensorHealth {
    states
        .iter()
        .fold(SensorHealth::default(), |h, &(valid, now)| {
            sensor_health_step(h, valid, now, HOLD_MS)
        })
}

#[test]
fn sensor_never_valid_does_not_report_lost() {
    let h = run(&[(false, 0), (false, 1000), (false, 60_000)]);
    assert!(!h.lost);
    assert!(!h.ever_valid);
}

#[test]
fn sensor_valid_then_invalid_is_lost() {
    let h = run(&[(true, 0), (false, 2000)]);
    assert!(h.lost);
}

#[test]
fn sensor_stays_lost_while_invalid() {
    let h = run(&[(true, 0), (false, 2000), (false, 100_000)]);
    assert!(h.lost);
}

#[test]
fn sensor_lost_not_cleared_before_hold_elapses() {
    let h = run(&[
        (true, 0),
        (false, 2000),
        (true, 3000),
        (true, 3000 + HOLD_MS - 1),
    ]);
    assert!(h.lost);
}

#[test]
fn sensor_lost_cleared_after_continuous_validity_hold() {
    let h = run(&[
        (true, 0),
        (false, 2000),
        (true, 3000),
        (true, 3000 + HOLD_MS),
    ]);
    assert!(!h.lost);
}

#[test]
fn bounce_during_hold_restarts_the_clock() {
    let h = run(&[
        (true, 0),
        (false, 2000),
        (true, 3000),
        (false, 4000),
        (true, 5000),
        (true, 5000 + HOLD_MS - 1),
    ]);
    assert!(h.lost);
    let h = sensor_health_step(h, true, 5000 + HOLD_MS, HOLD_MS);
    assert!(!h.lost);
}

#[test]
fn relost_after_clear_fires_again() {
    let h = run(&[
        (true, 0),
        (false, 2000),
        (true, 3000),
        (true, 3000 + HOLD_MS),
    ]);
    assert!(!h.lost);
    let h = sensor_health_step(h, false, 20_000, HOLD_MS);
    assert!(h.lost);
}

#[test]
fn hold_survives_millis_wraparound() {
    let near_wrap = u32::MAX - 1000;
    let h = run(&[
        (true, near_wrap - 5000),
        (false, near_wrap - 1000),
        (true, near_wrap),
    ]);
    assert!(h.lost);
    let h = sensor_health_step(h, true, near_wrap.wrapping_add(HOLD_MS), HOLD_MS);
    assert!(!h.lost);
}

const MIN_ACTIVE_MS: u32 = 2000;
const HYST_PCT: f32 = 2.0;

fn hyst_step(h: LevelHold, value: f32, now_ms: u32) -> LevelHold {
    eval_with_hysteresis(h, value, now_ms, HYST_PCT, MIN_ACTIVE_MS, |v| {
        eval_high_side(v, 100.0, 110.0)
    })
}

#[test]
fn crossing_up_fires_immediately() {
    let h = hyst_step(LevelHold::default(), 101.0, 0);
    assert_eq!(h.level, AlertLevel::Warning);
    assert_eq!(h.raised_at_ms, 0);
}

#[test]
fn escalation_bypasses_min_hold() {
    let h = hyst_step(LevelHold::default(), 101.0, 0);
    let h = hyst_step(h, 115.0, 100);
    assert_eq!(h.level, AlertLevel::Critical);
    assert_eq!(h.raised_at_ms, 100);
}

#[test]
fn hover_just_below_threshold_stays_within_deadband() {
    let h = hyst_step(LevelHold::default(), 101.0, 0);
    let h = hyst_step(h, 99.5, 5000);
    assert_eq!(h.level, AlertLevel::Warning);
}

#[test]
fn clears_only_after_receding_past_deadband() {
    let h = hyst_step(LevelHold::default(), 101.0, 0);
    let h = hyst_step(h, 97.5, 5000);
    assert_eq!(h.level, AlertLevel::Normal);
}

#[test]
fn min_hold_keeps_level_after_brief_spike() {
    let h = hyst_step(LevelHold::default(), 115.0, 0);
    let h = hyst_step(h, 50.0, 500);
    assert_eq!(h.level, AlertLevel::Critical);
    let h = hyst_step(h, 50.0, MIN_ACTIVE_MS - 1);
    assert_eq!(h.level, AlertLevel::Critical);
    let h = hyst_step(h, 50.0, MIN_ACTIVE_MS);
    assert_eq!(h.level, AlertLevel::Normal);
}

#[test]
fn drop_lands_on_sticky_intermediate_level() {
    let h = hyst_step(LevelHold::default(), 115.0, 0);
    let h = hyst_step(h, 105.0, 5000);
    assert_eq!(h.level, AlertLevel::Warning);
    assert_eq!(h.raised_at_ms, 5000);
}

#[test]
fn low_side_deadband_holds_and_clears() {
    let batt = |h: LevelHold, volts: f32, now: u32| {
        eval_with_hysteresis(h, volts, now, HYST_PCT, MIN_ACTIVE_MS, |v| {
            eval_battery(v, 11.5, 11.0, f32::NAN, f32::NAN)
        })
    };
    let h = batt(LevelHold::default(), 11.4, 0);
    assert_eq!(h.level, AlertLevel::Warning);
    let h = batt(h, 11.6, 5000);
    assert_eq!(h.level, AlertLevel::Warning);
    let h = batt(h, 11.8, 6000);
    assert_eq!(h.level, AlertLevel::Normal);
}

#[test]
fn min_hold_survives_millis_wraparound() {
    let near_wrap = u32::MAX - 500;
    let h = hyst_step(LevelHold::default(), 115.0, near_wrap);
    let h = hyst_step(h, 50.0, near_wrap.wrapping_add(MIN_ACTIVE_MS - 1));
    assert_eq!(h.level, AlertLevel::Critical);
    let h = hyst_step(h, 50.0, near_wrap.wrapping_add(MIN_ACTIVE_MS));
    assert_eq!(h.level, AlertLevel::Normal);
}
