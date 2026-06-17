// Rust port of sensor_color_ramp.cpp (#1177 R-3).
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

// Mirror of CFG_MAX_RAMP_STOPS — bumping requires growing the FFI struct.
pub const MAX_RAMP_STOPS: usize = 8;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum RampInterp {
    Linear = 0,
    Step = 1,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RampStop {
    pub value: f32,
    /// 0x00RRGGBB packed integer.
    pub color: u32,
}

/// Only the first `count` entries of `stops` are valid.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct ColorRamp {
    pub count: u8,
    pub interpolate: RampInterp,
    pub _padding: [u8; 2],
    pub stops: [RampStop; MAX_RAMP_STOPS],
}

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SensorKind {
    Coolant = 0,
    OilTemp = 1,
    OilPress = 2,
    BatteryVolts = 3,
    Rpm = 4,
    Afr = 5,
    Boost = 6,
    IntakeTemp = 7,
    Egt = 8,
    Unknown = 9,
}

pub const SENSOR_KIND_COUNT: usize = 9;

const fn stop(value: f32, color: u32) -> RampStop {
    RampStop { value, color }
}

const fn ramp(count: u8, stops: [RampStop; MAX_RAMP_STOPS]) -> ColorRamp {
    ColorRamp {
        count,
        interpolate: RampInterp::Linear,
        _padding: [0; 2],
        stops,
    }
}

const ZERO: RampStop = stop(0.0, 0);

/// The default catalog. Index by `SensorKind as usize` (0..=8). `Unknown` (9)
/// has no entry.
pub const SENSOR_DEFAULT_RAMPS: [ColorRamp; SENSOR_KIND_COUNT] = [
    // Coolant
    ramp(
        4,
        [
            stop(60.0, 0x4A90E2),
            stop(90.0, 0x44CC66),
            stop(100.0, 0xCC8800),
            stop(110.0, 0xCC3333),
            ZERO,
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // OilTemp
    ramp(
        4,
        [
            stop(70.0, 0x4A90E2),
            stop(95.0, 0x44CC66),
            stop(120.0, 0xCC8800),
            stop(135.0, 0xCC3333),
            ZERO,
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // OilPress
    ramp(
        4,
        [
            stop(1.0, 0xCC3333),
            stop(1.8, 0xCC8800),
            stop(2.5, 0x44CC66),
            stop(6.0, 0x44CC66),
            ZERO,
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // BatteryVolts
    ramp(
        5,
        [
            stop(11.5, 0xCC3333),
            stop(12.5, 0xCC8800),
            stop(13.5, 0x44CC66),
            stop(14.8, 0xCC8800),
            stop(15.5, 0xCC3333),
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // Rpm
    ramp(
        4,
        [
            stop(1500.0, 0x44CC66),
            stop(5500.0, 0x44CC66),
            stop(6500.0, 0xCC8800),
            stop(7000.0, 0xCC3333),
            ZERO,
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // Afr
    ramp(
        5,
        [
            stop(10.5, 0xCC3333),
            stop(11.8, 0xCC8800),
            stop(13.0, 0x44CC66),
            stop(14.7, 0x44CC66),
            stop(16.0, 0xCC8800),
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // Boost
    ramp(
        4,
        [
            stop(0.0, 0x44CC66),
            stop(1.0, 0x44CC66),
            stop(1.4, 0xCC8800),
            stop(1.7, 0xCC3333),
            ZERO,
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // IntakeTemp
    ramp(
        3,
        [
            stop(20.0, 0x44CC66),
            stop(50.0, 0xCC8800),
            stop(65.0, 0xCC3333),
            ZERO,
            ZERO,
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
    // Egt
    ramp(
        3,
        [
            stop(600.0, 0x44CC66),
            stop(850.0, 0xCC8800),
            stop(950.0, 0xCC3333),
            ZERO,
            ZERO,
            ZERO,
            ZERO,
            ZERO,
        ],
    ),
];

/// Specific patterns must come before general ones (`oil_temp` before `oil`).
struct NameRule {
    pattern: &'static [u8],
    kind: SensorKind,
}

const NAME_RULES: &[NameRule] = &[
    NameRule { pattern: b"coolant", kind: SensorKind::Coolant },
    NameRule { pattern: b"oil_press", kind: SensorKind::OilPress },
    NameRule { pattern: b"oil_pressure", kind: SensorKind::OilPress },
    NameRule { pattern: b"oil_temp", kind: SensorKind::OilTemp },
    NameRule { pattern: b"oil", kind: SensorKind::OilTemp },
    NameRule { pattern: b"battery", kind: SensorKind::BatteryVolts },
    NameRule { pattern: b"batt_v", kind: SensorKind::BatteryVolts },
    NameRule { pattern: b"rpm", kind: SensorKind::Rpm },
    NameRule { pattern: b"afr", kind: SensorKind::Afr },
    NameRule { pattern: b"lambda", kind: SensorKind::Afr },
    NameRule { pattern: b"boost", kind: SensorKind::Boost },
    NameRule { pattern: b"manifold_press", kind: SensorKind::Boost },
    NameRule { pattern: b"map_press", kind: SensorKind::Boost },
    NameRule { pattern: b"intake_temp", kind: SensorKind::IntakeTemp },
    NameRule { pattern: b"iat", kind: SensorKind::IntakeTemp },
    NameRule { pattern: b"mat", kind: SensorKind::IntakeTemp },
    NameRule { pattern: b"egt", kind: SensorKind::Egt },
    NameRule { pattern: b"exhaust_temp", kind: SensorKind::Egt },
];

/// ASCII case-insensitive contains. Empty needle returns true (mirrors C++).
fn contains_ci(haystack: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    if needle.len() > haystack.len() {
        return false;
    }
    for i in 0..=haystack.len() - needle.len() {
        let mut matched = true;
        for j in 0..needle.len() {
            if haystack[i + j].to_ascii_lowercase() != needle[j].to_ascii_lowercase() {
                matched = false;
                break;
            }
        }
        if matched {
            return true;
        }
    }
    false
}

/// First match wins per NAME_RULES order. Empty input → Unknown.
#[must_use]
pub fn sensor_kind_from_name(signal_name: &[u8]) -> SensorKind {
    if signal_name.is_empty() {
        return SensorKind::Unknown;
    }
    for rule in NAME_RULES {
        if contains_ci(signal_name, rule.pattern) {
            return rule.kind;
        }
    }
    SensorKind::Unknown
}

/// half-away-from-zero rounding for ±1 LSB parity with std::lround.
#[must_use]
pub fn lerp_rgb(a: u32, b: u32, t: f32) -> u32 {
    let t = t.clamp(0.0, 1.0);
    let channel = |c: u32, shift: u32| ((c >> shift) & 0xFF) as i32;
    let ar = channel(a, 16);
    let ag = channel(a, 8);
    let ab = channel(a, 0);
    let br = channel(b, 16);
    let bg = channel(b, 8);
    let bb = channel(b, 0);
    let mix = |x: i32, y: i32| -> u32 {
        let v = (x as f32) + (y as f32 - x as f32) * t;
        let clamped = v.clamp(0.0, 255.0);

        (clamped + 0.5) as u32
    };
    (mix(ar, br) << 16) | (mix(ag, bg) << 8) | mix(ab, bb)
}

/// Below first stop → first color; above last → last; empty ramp → 0x000000.
#[must_use]
pub fn color_at_value(ramp: &ColorRamp, value: f32) -> u32 {
    if ramp.count == 0 {
        return 0x000000;
    }
    let count = ramp.count as usize;
    let stops = &ramp.stops[..count];
    let first = stops[0];
    let last = stops[count - 1];

    if count == 1 || value <= first.value {
        return first.color;
    }
    if value >= last.value {
        return last.color;
    }

    if ramp.interpolate == RampInterp::Step {
        let mut active = first.color;
        for s in stops {
            if value >= s.value {
                active = s.color;
            } else {
                break;
            }
        }
        return active;
    }

    for window in stops.windows(2) {
        let lower = window[0];
        let upper = window[1];
        if value >= lower.value && value <= upper.value {
            let span = upper.value - lower.value;
            let t = if span > 0.0 {
                (value - lower.value) / span
            } else {
                0.0
            };
            return lerp_rgb(lower.color, upper.color, t);
        }
    }

    last.color
}

/// Resolve the active ramp for a widget. Prefers the per-signal ramp; falls
/// back to the sensor-name heuristic; returns `None` when neither resolves
/// — the C++ caller keeps its legacy static-color path.
#[must_use]
pub fn resolve_ramp<'a>(
    per_signal: &'a ColorRamp,
    signal_name: &[u8],
) -> Option<&'a ColorRamp> {
    if per_signal.count > 0 {
        return Some(per_signal);
    }
    let kind = sensor_kind_from_name(signal_name);
    if kind == SensorKind::Unknown {
        return None;
    }
    Some(&SENSOR_DEFAULT_RAMPS[kind as usize])
}

#[cfg(test)]
mod tests {
    use super::*;

    // --- contains_ci ---------------------------------------------------------

    #[test]
    fn contains_ci_basic_match() {
        assert!(contains_ci(b"coolant_temp_c", b"coolant"));
    }

    #[test]
    fn contains_ci_case_insensitive() {
        assert!(contains_ci(b"COOLANT_TEMP", b"coolant"));
        assert!(contains_ci(b"coolant_TEMP", b"COOLANT"));
    }

    #[test]
    fn contains_ci_no_match() {
        assert!(!contains_ci(b"oil_pressure", b"coolant"));
    }

    #[test]
    fn contains_ci_empty_needle_is_true() {
        assert!(contains_ci(b"anything", b""));
    }

    #[test]
    fn contains_ci_needle_longer_than_haystack_is_false() {
        assert!(!contains_ci(b"abc", b"abcd"));
    }

    // --- sensor_kind_from_name ----------------------------------------------

    #[test]
    fn name_coolant() {
        assert_eq!(sensor_kind_from_name(b"coolant_temp_c"), SensorKind::Coolant);
    }

    #[test]
    fn name_oil_press_before_oil_temp() {
        // Order in NAME_RULES — `oil_press` checked before `oil` (which would
        // otherwise capture it as OilTemp).
        assert_eq!(sensor_kind_from_name(b"oil_pressure"), SensorKind::OilPress);
        assert_eq!(sensor_kind_from_name(b"oil_press_bar"), SensorKind::OilPress);
    }

    #[test]
    fn name_oil_temp() {
        assert_eq!(sensor_kind_from_name(b"oil_temp_c"), SensorKind::OilTemp);
    }

    #[test]
    fn name_oil_falls_through_to_oil_temp() {
        // No `oil_temp` / `oil_press` substring — generic `oil` rule matches.
        assert_eq!(sensor_kind_from_name(b"engine_oil"), SensorKind::OilTemp);
    }

    #[test]
    fn name_battery() {
        assert_eq!(sensor_kind_from_name(b"battery_volts"), SensorKind::BatteryVolts);
        assert_eq!(sensor_kind_from_name(b"batt_v"), SensorKind::BatteryVolts);
    }

    #[test]
    fn name_rpm() {
        assert_eq!(sensor_kind_from_name(b"engine_rpm"), SensorKind::Rpm);
    }

    #[test]
    fn name_lambda_routes_to_afr() {
        assert_eq!(sensor_kind_from_name(b"lambda_short"), SensorKind::Afr);
    }

    #[test]
    fn name_boost_synonyms() {
        assert_eq!(sensor_kind_from_name(b"boost_psi"), SensorKind::Boost);
        assert_eq!(sensor_kind_from_name(b"manifold_press"), SensorKind::Boost);
        assert_eq!(sensor_kind_from_name(b"map_press"), SensorKind::Boost);
    }

    #[test]
    fn name_intake_temp_synonyms() {
        assert_eq!(sensor_kind_from_name(b"intake_temp_c"), SensorKind::IntakeTemp);
        assert_eq!(sensor_kind_from_name(b"iat"), SensorKind::IntakeTemp);
        assert_eq!(sensor_kind_from_name(b"mat"), SensorKind::IntakeTemp);
    }

    #[test]
    fn name_egt() {
        assert_eq!(sensor_kind_from_name(b"egt_c"), SensorKind::Egt);
        assert_eq!(sensor_kind_from_name(b"exhaust_temp"), SensorKind::Egt);
    }

    #[test]
    fn name_unknown() {
        assert_eq!(sensor_kind_from_name(b"random_signal"), SensorKind::Unknown);
        assert_eq!(sensor_kind_from_name(b""), SensorKind::Unknown);
    }

    // --- lerp_rgb ------------------------------------------------------------

    #[test]
    fn lerp_at_zero_returns_a() {
        assert_eq!(lerp_rgb(0x44CC66, 0xCC8800, 0.0), 0x44CC66);
    }

    #[test]
    fn lerp_at_one_returns_b() {
        assert_eq!(lerp_rgb(0x44CC66, 0xCC8800, 1.0), 0xCC8800);
    }

    #[test]
    fn lerp_clamps_negative() {
        assert_eq!(lerp_rgb(0x44CC66, 0xCC8800, -0.5), 0x44CC66);
    }

    #[test]
    fn lerp_clamps_over_one() {
        assert_eq!(lerp_rgb(0x44CC66, 0xCC8800, 1.5), 0xCC8800);
    }

    #[test]
    fn lerp_midpoint_channel_average() {
        // 0x00 + 0xFF averaged with half is 0x7F or 0x80 depending on rounding
        // — C++ std::lround uses half-away-from-zero, so 127.5 → 128 (0x80).
        let result = lerp_rgb(0x000000, 0xFFFFFF, 0.5);
        assert_eq!(result, 0x808080);
    }

    // --- color_at_value ------------------------------------------------------

    #[test]
    fn color_at_value_empty_ramp_returns_black() {
        let empty = ramp(0, [ZERO; MAX_RAMP_STOPS]);
        assert_eq!(color_at_value(&empty, 100.0), 0x000000);
    }

    #[test]
    fn color_at_value_below_first_returns_first() {
        let r = &SENSOR_DEFAULT_RAMPS[SensorKind::Coolant as usize];
        assert_eq!(color_at_value(r, 30.0), 0x4A90E2);
    }

    #[test]
    fn color_at_value_above_last_returns_last() {
        let r = &SENSOR_DEFAULT_RAMPS[SensorKind::Coolant as usize];
        assert_eq!(color_at_value(r, 200.0), 0xCC3333);
    }

    #[test]
    fn color_at_value_exact_stop_value() {
        let r = &SENSOR_DEFAULT_RAMPS[SensorKind::Coolant as usize];
        assert_eq!(color_at_value(r, 90.0), 0x44CC66); // exact second stop
    }

    #[test]
    fn color_at_value_linear_midpoint() {
        let r = &SENSOR_DEFAULT_RAMPS[SensorKind::Coolant as usize];
        // Midpoint between (90, 0x44CC66) and (100, 0xCC8800).
        let mid = color_at_value(r, 95.0);
        let expected = lerp_rgb(0x44CC66, 0xCC8800, 0.5);
        assert_eq!(mid, expected);
    }

    #[test]
    fn color_at_value_step_mode() {
        let r = ColorRamp {
            count: 3,
            interpolate: RampInterp::Step,
            _padding: [0; 2],
            stops: [
                stop(0.0, 0x44CC66),
                stop(50.0, 0xCC8800),
                stop(100.0, 0xCC3333),
                ZERO,
                ZERO,
                ZERO,
                ZERO,
                ZERO,
            ],
        };
        assert_eq!(color_at_value(&r, 25.0), 0x44CC66);
        assert_eq!(color_at_value(&r, 50.0), 0xCC8800);
        assert_eq!(color_at_value(&r, 75.0), 0xCC8800);
        assert_eq!(color_at_value(&r, 99.0), 0xCC8800);
        assert_eq!(color_at_value(&r, 100.0), 0xCC3333);
    }

    #[test]
    fn color_at_value_single_stop_returns_that_color() {
        let r = ramp(
            1,
            [
                stop(42.0, 0xABCDEF),
                ZERO,
                ZERO,
                ZERO,
                ZERO,
                ZERO,
                ZERO,
                ZERO,
            ],
        );
        assert_eq!(color_at_value(&r, 0.0), 0xABCDEF);
        assert_eq!(color_at_value(&r, 42.0), 0xABCDEF);
        assert_eq!(color_at_value(&r, 1000.0), 0xABCDEF);
    }

    // --- resolve_ramp --------------------------------------------------------

    #[test]
    fn resolve_ramp_prefers_per_signal_when_non_empty() {
        let per = ramp(
            1,
            [stop(0.0, 0x123456), ZERO, ZERO, ZERO, ZERO, ZERO, ZERO, ZERO],
        );
        let r = resolve_ramp(&per, b"coolant_temp_c").unwrap();
        assert_eq!(r.count, 1);
        assert_eq!(r.stops[0].color, 0x123456);
    }

    #[test]
    fn resolve_ramp_falls_back_to_default_when_per_signal_is_empty() {
        let per = ramp(0, [ZERO; MAX_RAMP_STOPS]);
        let r = resolve_ramp(&per, b"coolant_temp_c").unwrap();
        assert_eq!(r.count, 4);
        assert_eq!(r.stops[0].color, 0x4A90E2);
    }

    #[test]
    fn resolve_ramp_returns_none_for_unknown_signal_with_empty_per_signal() {
        let per = ramp(0, [ZERO; MAX_RAMP_STOPS]);
        assert!(resolve_ramp(&per, b"mystery_signal").is_none());
    }

    // --- struct layout proof -------------------------------------------------

    #[test]
    fn ramp_stop_size_matches_cxx() {
        assert_eq!(core::mem::size_of::<RampStop>(), 8);
    }

    #[test]
    fn color_ramp_size_matches_cxx() {
        // u8 + u8 (RampInterp) + 2 padding + 8 * (RampStop = 8) = 68 bytes
        assert_eq!(core::mem::size_of::<ColorRamp>(), 4 + MAX_RAMP_STOPS * 8);
    }
}
