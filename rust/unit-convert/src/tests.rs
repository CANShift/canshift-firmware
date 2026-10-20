use super::*;

fn symbol(unit: &str, system: UnitSystem) -> Option<&'static str> {
    display_symbol(unit.as_bytes(), system)
        .map(|s| core::str::from_utf8(&s[..s.len() - 1]).unwrap())
}

fn value(v: f32, unit: &str, system: UnitSystem) -> f32 {
    display_value(v, unit.as_bytes(), system)
}

#[test]
fn speed_converts_to_mph_and_keeps_kmh_for_metric() {
    assert!((value(100.0, "km/h", UnitSystem::Imperial) - 62.137_12).abs() < 0.001);
    assert_eq!(value(100.0, "km/h", UnitSystem::Metric), 100.0);
    assert_eq!(symbol("km/h", UnitSystem::Imperial), Some("mph"));
    assert_eq!(symbol("km/h", UnitSystem::Metric), Some("km/h"));
}

#[test]
fn celsius_converts_through_the_offset_not_a_ratio() {
    assert!((value(100.0, "°C", UnitSystem::Imperial) - 212.0).abs() < 0.001);
    assert!((value(0.0, "°C", UnitSystem::Imperial) - 32.0).abs() < 0.001);
    assert_eq!(symbol("°C", UnitSystem::Imperial), Some("°F"));
}

#[test]
fn pressure_converts_from_both_kpa_and_bar() {
    assert!((value(100.0, "kPa", UnitSystem::Imperial) - 14.503_774).abs() < 0.001);
    assert!((value(1.0, "bar", UnitSystem::Imperial) - 14.503_774).abs() < 0.001);
    assert_eq!(symbol("bar", UnitSystem::Imperial), Some("psi"));
}

#[test]
fn psi_resolves_back_to_kpa_not_bar() {
    assert_eq!(symbol("psi", UnitSystem::Metric), Some("kPa"));
    assert!((value(14.503_774, "psi", UnitSystem::Metric) - 100.0).abs() < 0.01);
}

#[test]
fn distance_converts_to_miles() {
    assert!((value(10.0, "km", UnitSystem::Imperial) - 6.213_712).abs() < 0.001);
    assert_eq!(symbol("km", UnitSystem::Imperial), Some("mi"));
}

#[test]
fn an_unpaired_unit_is_untouched() {
    for unit in ["%", "V", "rpm", "", "λ"] {
        assert_eq!(value(42.0, unit, UnitSystem::Imperial), 42.0);
        assert_eq!(symbol(unit, UnitSystem::Imperial), None);
    }
}

#[test]
fn aliases_and_casing_resolve_to_the_same_pair() {
    for unit in ["kph", "KM/H", "km/hr", " km/h "] {
        assert_eq!(symbol(unit, UnitSystem::Imperial), Some("mph"));
    }
    for unit in ["C", "degC", "c"] {
        assert_eq!(symbol(unit, UnitSystem::Imperial), Some("°F"));
    }
    assert_eq!(symbol("miles", UnitSystem::Metric), Some("km"));
}

#[test]
fn a_unit_longer_than_the_buffer_finds_no_pair() {
    assert_eq!(symbol("kilometres/hour", UnitSystem::Imperial), None);
    assert_eq!(value(1.0, "kilometres/hour", UnitSystem::Imperial), 1.0);
}

#[test]
fn a_round_trip_returns_the_declared_value() {
    for (unit, v) in [
        ("km/h", 137.0f32),
        ("°C", 92.0),
        ("kPa", 450.0),
        ("km", 3.2),
    ] {
        let shown = display_value(v, unit.as_bytes(), UnitSystem::Imperial);
        let back = display_value(
            shown,
            display_symbol(unit.as_bytes(), UnitSystem::Imperial).unwrap(),
            UnitSystem::Metric,
        );
        assert!((back - v).abs() < 0.01, "{unit} round trip drifted");
    }
}

#[test]
fn bar_drops_a_decimal_once_it_reads_in_psi() {
    assert_eq!(display_decimals(1, b"bar", UnitSystem::Imperial), 0);
    assert_eq!(display_decimals(2, b"bar", UnitSystem::Imperial), 1);
    assert_eq!(display_decimals(1, b"bar", UnitSystem::Metric), 1);
}

#[test]
fn psi_drops_a_decimal_once_it_reads_in_kpa() {
    assert_eq!(display_decimals(1, b"psi", UnitSystem::Metric), 0);
    assert_eq!(display_decimals(1, b"kPa", UnitSystem::Imperial), 1);
}

#[test]
fn decimals_never_go_below_zero() {
    assert_eq!(display_decimals(0, b"bar", UnitSystem::Imperial), 0);
}

#[test]
fn a_conversion_that_gains_no_digit_keeps_its_decimals() {
    for unit in ["km/h", "km", "°C", "rpm", ""] {
        assert_eq!(
            display_decimals(2, unit.as_bytes(), UnitSystem::Imperial),
            2
        );
    }
    for unit in ["mph", "mi", "°F"] {
        assert_eq!(display_decimals(2, unit.as_bytes(), UnitSystem::Metric), 2);
    }
}

#[test]
fn from_u8_defaults_anything_unknown_to_metric() {
    assert_eq!(
        UnitSystem::from_u8(UNIT_SYSTEM_IMPERIAL),
        UnitSystem::Imperial
    );
    assert_eq!(UnitSystem::from_u8(UNIT_SYSTEM_METRIC), UnitSystem::Metric);
    assert_eq!(UnitSystem::from_u8(7), UnitSystem::Metric);
}
