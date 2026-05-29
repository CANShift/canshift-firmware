// alert-engine — Rust port of AlertEngine threshold-eval helpers.
//
// Lives in `canshift-firmware/src/runtime/alert_engine.cpp` originally:
// six anonymous-namespace `eval*` functions that classify a single signal
// value (`tempC`, `pressBar`, `volts`, `rpm`) against per-signal thresholds
// loaded from the dashboard config. The functions are pure given the
// threshold inputs — they touch no globals, no I/O, no LVGL state.
//
// What stays C++:
//   - Module-level static thresholds (`s_coolantWarnC`, `s_oilTempCritC`, …)
//     loaded by `init()` from `signals.json` at boot.
//   - `tick()` orchestration — reads signals via `SignalStore`, calls each
//     `eval*` helper, writes `s_state`, manages the flash-effect phase.
//   - The `AlertState` snapshot and the rev-limiter flash UI helpers.
//
// What moves to Rust:
//   - The six per-signal `eval*` helpers. Each is a small if/else chain
//     over (mostly NaN-aware) float comparisons. This is exactly the
//     pattern where C++ `value > NaN`-returns-false silently lets a
//     sensor-failure value (NaN) escape the alert chain — Rust pattern
//     matching makes the NaN behaviour visible and locked-down by the
//     parity tests.
//
// The crate exposes ONE entry point shape: `(value, …thresholds) ->
// AlertLevel`. The C++ wrapper passes its static thresholds at the call
// site.

#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

// Panic handler — required for `no_std + staticlib`. Halts forever. Same
// strategy as the other ports. All public functions return `AlertLevel`
// (no failure mode) on any input; reaching the handler means an internal
// invariant break.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

// Mirror of `AlertEngine::AlertLevel` from `alert_engine.h`. `#[repr(u8)]`
// is load-bearing — the C++ side stores this in a `uint8_t` underlying
// enum, and the FFI shim returns it directly.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub enum AlertLevel {
    Normal = 0,
    Caution = 1,
    Warning = 2,
    Critical = 3,
}

impl AlertLevel {
    // Mirror of the C++ unnamed-namespace `maxLevel` helper. Returns the
    // more severe of `self` and `other`. Uses `Ord` on the discriminant.
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

// Behaviour preserved from C++ — both `evalHighSide` operands match the
// `>` operator, so:
//   - NaN value → returns NORMAL (NaN > anything is false). This is the
//     "sensor failure → silent" case documented in the impl comment.
//   - NaN threshold → "disabled" (the explicit `!isnan(threshold)` guard).
//
// Returns the highest matching level. `high_crit` outranks `high_warn`
// when both fire.
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

// Rev limiter: `warn_pct` / `flash_pct` are integer percentages of
// `rev_limit_rpm`. Mirrors `evalRevLimiter` byte-for-byte.
//
// `>=` semantics (not `>`) — at the exact flash percent the alert fires.
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

// Coolant temperature: 4-level chain (NORMAL → CAUTION → WARNING →
// CRITICAL). The CAUTION band is `warn_c - 5.0` — a magic constant in the
// C++ original (a hard-coded 5 °C "approaching warning" pre-band). Preserved
// here for byte-for-byte parity. If the value lands in CRITICAL via the
// `>=` chain, the high-side helper can still escalate further but never
// downgrade (`max_level` is monotonic).
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

// Oil temperature: 3-level chain (no CAUTION pre-band). Otherwise same
// shape as `eval_coolant_temp`.
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

// Oil pressure: LOW side is the primary alert (cranking failure / pump
// failure). Optional high-side via `high_warn_bar` / `high_crit_bar`.
// `<=` semantics on the low side: at the exact crit value, fire.
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

// Battery voltage: low side is dominant (cranking failure). The C++
// returns CRITICAL immediately on `volts < low_crit_v` without checking
// the high side — preserved here to match the existing semantics. High
// side is only consulted when the value is above the low-warn band.
//
// `<` semantics (not `<=`) on the low side — preserves C++ original.
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

    // --- AlertLevel::max -----------------------------------------------------

    #[test]
    fn max_level_picks_more_severe() {
        assert_eq!(AlertLevel::Warning.max(AlertLevel::Normal), AlertLevel::Warning);
        assert_eq!(AlertLevel::Critical.max(AlertLevel::Warning), AlertLevel::Critical);
        assert_eq!(AlertLevel::Caution.max(AlertLevel::Caution), AlertLevel::Caution);
    }

    // --- eval_high_side ------------------------------------------------------

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
        // High-warn NaN, high-crit NaN → no escalation regardless of value.
        assert_eq!(eval_high_side(999.0, f32::NAN, f32::NAN), AlertLevel::Normal);
    }

    #[test]
    fn high_side_only_crit_configured() {
        // High-warn NaN, high-crit 100 — over crit fires CRITICAL, below stays NORMAL.
        assert_eq!(eval_high_side(50.0, f32::NAN, 100.0), AlertLevel::Normal);
        assert_eq!(eval_high_side(150.0, f32::NAN, 100.0), AlertLevel::Critical);
    }

    #[test]
    fn high_side_nan_value_is_normal_documented_quirk() {
        // NaN > anything → false. C++ `evalHighSide` silently returns NORMAL on
        // a NaN sensor reading; we preserve that. The fix lives at the caller —
        // tick() should drop NaN signals before classifying them. This test
        // locks the contract so we don't accidentally change it in a refactor.
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
}
