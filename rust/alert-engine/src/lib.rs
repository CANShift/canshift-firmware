// Rust port of AlertEngine eval* helpers — pure (value, thresholds) → AlertLevel.
// NaN value → Normal; NaN threshold → disabled (mirrors C++ via `>` semantics).
#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

// Required for no_std staticlib — reaching here means invariant break.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

// repr(u8) is load-bearing — C++ enum is uint8_t.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub enum AlertLevel {
    Normal = 0,
    Caution = 1,
    Warning = 2,
    Critical = 3,
}

impl AlertLevel {
    #[inline]
    #[must_use]
    pub fn max(self, other: Self) -> Self {
        if (self as u8) > (other as u8) {
            self
        } else {
            other
        }
    }
}

// `ever_valid` gates the boot window (no CAN yet ≠ broken sensor); the
// clear-hold keeps a marginal sensor bouncing valid/invalid from strobing UI.
#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct SensorHealth {
    pub ever_valid: bool,
    pub lost: bool,
    pub prev_valid: bool,
    pub valid_since_ms: u32,
}

#[must_use]
pub fn sensor_health_step(
    mut h: SensorHealth,
    valid: bool,
    now_ms: u32,
    clear_hold_ms: u32,
) -> SensorHealth {
    if valid {
        if !h.prev_valid {
            h.valid_since_ms = now_ms;
        }
        h.ever_valid = true;
        if h.lost && now_ms.wrapping_sub(h.valid_since_ms) >= clear_hold_ms {
            h.lost = false;
        }
    } else if h.ever_valid {
        h.lost = true;
    }
    h.prev_valid = valid;
    h
}

#[must_use]
pub fn eval_high_side(value: f32, high_warn: f32, high_crit: f32) -> AlertLevel {
    if !high_crit.is_nan() && value > high_crit {
        return AlertLevel::Critical;
    }
    if !high_warn.is_nan() && value > high_warn {
        return AlertLevel::Warning;
    }
    AlertLevel::Normal
}

#[must_use]
pub fn eval_rev_limiter(rpm: f32, rev_limit_rpm: f32, warn_pct: u8, flash_pct: u8) -> AlertLevel {
    let warn_rpm = rev_limit_rpm * (warn_pct as f32 / 100.0);
    let flash_rpm = rev_limit_rpm * (flash_pct as f32 / 100.0);
    if rpm >= flash_rpm {
        return AlertLevel::Critical;
    }
    if rpm >= warn_rpm {
        return AlertLevel::Warning;
    }
    AlertLevel::Normal
}

// CAUTION pre-band is `warn_c - 5°C` (mirrors C++ hard-coded constant).
#[must_use]
pub fn eval_coolant_temp(
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
) -> AlertLevel {
    let base = if temp_c >= crit_c {
        AlertLevel::Critical
    } else if temp_c >= warn_c {
        AlertLevel::Warning
    } else if temp_c >= warn_c - 5.0 {
        AlertLevel::Caution
    } else {
        AlertLevel::Normal
    };
    base.max(eval_high_side(temp_c, high_warn_c, high_crit_c))
}

#[must_use]
pub fn eval_oil_temp(
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
) -> AlertLevel {
    let base = if temp_c >= crit_c {
        AlertLevel::Critical
    } else if temp_c >= warn_c {
        AlertLevel::Warning
    } else {
        AlertLevel::Normal
    };
    base.max(eval_high_side(temp_c, high_warn_c, high_crit_c))
}

// LOW side is primary (cranking/pump failure); high-side optional.
#[must_use]
pub fn eval_oil_pressure(
    press_bar: f32,
    warn_bar: f32,
    crit_bar: f32,
    high_warn_bar: f32,
    high_crit_bar: f32,
) -> AlertLevel {
    let base = if press_bar <= crit_bar {
        AlertLevel::Critical
    } else if press_bar <= warn_bar {
        AlertLevel::Warning
    } else {
        AlertLevel::Normal
    };
    base.max(eval_high_side(press_bar, high_warn_bar, high_crit_bar))
}

// Low-side critical short-circuits — high side is only consulted above low-warn.
#[must_use]
pub fn eval_battery(
    volts: f32,
    low_warn_v: f32,
    low_crit_v: f32,
    high_warn_v: f32,
    high_crit_v: f32,
) -> AlertLevel {
    if volts < low_crit_v {
        return AlertLevel::Critical;
    }
    let base = if volts < low_warn_v {
        AlertLevel::Warning
    } else {
        AlertLevel::Normal
    };
    base.max(eval_high_side(volts, high_warn_v, high_crit_v))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn max_level_picks_more_severe() {
        assert_eq!(AlertLevel::Warning.max(AlertLevel::Normal), AlertLevel::Warning);
        assert_eq!(AlertLevel::Critical.max(AlertLevel::Warning), AlertLevel::Critical);
        assert_eq!(AlertLevel::Caution.max(AlertLevel::Caution), AlertLevel::Caution);
    }

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
        assert_eq!(eval_high_side(999.0, f32::NAN, f32::NAN), AlertLevel::Normal);
    }

    #[test]
    fn high_side_only_crit_configured() {
        assert_eq!(eval_high_side(50.0, f32::NAN, 100.0), AlertLevel::Normal);
        assert_eq!(eval_high_side(150.0, f32::NAN, 100.0), AlertLevel::Critical);
    }

    #[test]
    fn high_side_nan_value_is_normal_documented_quirk() {
        // NaN > anything → false; caller must drop NaN before classifying.
        assert_eq!(eval_high_side(f32::NAN, 100.0, 110.0), AlertLevel::Normal);
    }

    #[test]
    fn high_side_strict_greater_not_geq() {
        // Exact threshold value → does NOT fire (matches C++ `value > highWarn`).
        assert_eq!(eval_high_side(100.0, 100.0, 110.0), AlertLevel::Normal);
        assert_eq!(eval_high_side(110.0, 100.0, 110.0), AlertLevel::Warning);
    }

    // --- eval_rev_limiter ----------------------------------------------------

    #[test]
    fn rev_limiter_under_warn_is_normal() {
        assert_eq!(eval_rev_limiter(5000.0, 7200.0, 95, 100), AlertLevel::Normal);
    }

    #[test]
    fn rev_limiter_at_warn_fires_warning() {
        // 95% of 7200 = 6840. `>=` semantics — exactly at the warn fires.
        assert_eq!(eval_rev_limiter(6840.0, 7200.0, 95, 100), AlertLevel::Warning);
    }

    #[test]
    fn rev_limiter_at_flash_fires_critical() {
        assert_eq!(eval_rev_limiter(7200.0, 7200.0, 95, 100), AlertLevel::Critical);
    }

    #[test]
    fn rev_limiter_over_flash_stays_critical() {
        assert_eq!(eval_rev_limiter(8000.0, 7200.0, 95, 100), AlertLevel::Critical);
    }

    // --- eval_coolant_temp ---------------------------------------------------

    #[test]
    fn coolant_normal_well_below_warn() {
        assert_eq!(
            eval_coolant_temp(80.0, 100.0, 110.0, f32::NAN, f32::NAN),
            AlertLevel::Normal
        );
    }

    #[test]
    fn coolant_caution_within_five_of_warn() {
        // CAUTION band is `warn - 5.0` to `warn`. 96..<100 are CAUTION.
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
        // Base would be NORMAL (50 < warn - 5 = 95), but high-side fires.
        assert_eq!(
            eval_coolant_temp(50.0, 100.0, 110.0, 40.0, 60.0),
            AlertLevel::Warning
        );
        // Base CRITICAL stays CRITICAL when high side is NaN.
        assert_eq!(
            eval_coolant_temp(120.0, 100.0, 110.0, f32::NAN, f32::NAN),
            AlertLevel::Critical
        );
    }

    // --- eval_oil_temp -------------------------------------------------------

    #[test]
    fn oil_temp_no_caution_band() {
        // Unlike coolant, oil temp jumps from NORMAL to WARNING with no
        // CAUTION pre-band. 119 < 120 → NORMAL.
        assert_eq!(
            eval_oil_temp(119.0, 120.0, 135.0, f32::NAN, f32::NAN),
            AlertLevel::Normal
        );
        assert_eq!(
            eval_oil_temp(120.0, 120.0, 135.0, f32::NAN, f32::NAN),
            AlertLevel::Warning
        );
    }

    // --- eval_oil_pressure ---------------------------------------------------

    #[test]
    fn oil_pressure_low_side_fires_at_crit() {
        // Low-side: <= crit → CRITICAL.
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
        // High side: over_pressure scenario. 8.0 > 7.0 (high-crit) → CRITICAL.
        assert_eq!(
            eval_oil_pressure(8.0, 1.5, 1.0, 6.0, 7.0),
            AlertLevel::Critical
        );
    }

    // --- eval_battery --------------------------------------------------------

    #[test]
    fn battery_below_low_crit_is_critical_immediately() {
        // `<` semantics — strictly below.
        assert_eq!(
            eval_battery(10.0, 11.5, 11.0, f32::NAN, f32::NAN),
            AlertLevel::Critical
        );
    }

    #[test]
    fn battery_at_low_crit_is_NOT_critical_strict_less_than() {
        // Exactly at low_crit_v: NOT < low_crit_v → falls through to the
        // low-warn check. 11.0 < 11.5 → WARNING.
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

    // --- sensor_health_step ----------------------------------------------------

    const HOLD_MS: u32 = 3000;

    fn run(states: &[(bool, u32)]) -> SensorHealth {
        states.iter().fold(SensorHealth::default(), |h, &(valid, now)| {
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
        let h = run(&[(true, 0), (false, 2000), (true, 3000), (true, 3000 + HOLD_MS - 1)]);
        assert!(h.lost);
    }

    #[test]
    fn sensor_lost_cleared_after_continuous_validity_hold() {
        let h = run(&[(true, 0), (false, 2000), (true, 3000), (true, 3000 + HOLD_MS)]);
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
        let h = run(&[(true, 0), (false, 2000), (true, 3000), (true, 3000 + HOLD_MS)]);
        assert!(!h.lost);
        let h = sensor_health_step(h, false, 20_000, HOLD_MS);
        assert!(h.lost);
    }

    #[test]
    fn hold_survives_millis_wraparound() {
        let near_wrap = u32::MAX - 1000;
        let h = run(&[(true, near_wrap - 5000), (false, near_wrap - 1000), (true, near_wrap)]);
        assert!(h.lost);
        let h = sensor_health_step(h, true, near_wrap.wrapping_add(HOLD_MS), HOLD_MS);
        assert!(!h.lost);
    }
}
