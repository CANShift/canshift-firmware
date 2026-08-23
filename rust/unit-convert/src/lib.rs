#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

pub const UNIT_SYSTEM_METRIC: u8 = 0;
pub const UNIT_SYSTEM_IMPERIAL: u8 = 1;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum UnitSystem {
    #[default]
    Metric = UNIT_SYSTEM_METRIC,
    Imperial = UNIT_SYSTEM_IMPERIAL,
}

impl UnitSystem {
    pub fn from_u8(raw: u8) -> Self {
        if raw == UNIT_SYSTEM_IMPERIAL {
            Self::Imperial
        } else {
            Self::Metric
        }
    }
}

const KMH_PER_MPH: f32 = 1.609_344;
const KM_PER_MI: f32 = 1.609_344;
const KPA_PER_PSI: f32 = 6.894_757_3;
const BAR_PER_PSI: f32 = KPA_PER_PSI / 100.0;
const FAHRENHEIT_PER_CELSIUS: f32 = 9.0 / 5.0;
const FAHRENHEIT_OFFSET: f32 = 32.0;

#[derive(Clone, Copy)]
enum Scale {
    Ratio(f32),
    Fahrenheit,
}

impl Scale {
    fn to_imperial(self, value: f32) -> f32 {
        match self {
            Self::Ratio(metric_per_imperial) => value / metric_per_imperial,
            Self::Fahrenheit => value * FAHRENHEIT_PER_CELSIUS + FAHRENHEIT_OFFSET,
        }
    }

    fn to_metric(self, value: f32) -> f32 {
        match self {
            Self::Ratio(metric_per_imperial) => value * metric_per_imperial,
            Self::Fahrenheit => (value - FAHRENHEIT_OFFSET) / FAHRENHEIT_PER_CELSIUS,
        }
    }
}

pub struct UnitPair {
    metric: &'static [u8],
    imperial: &'static [u8],
    scale: Scale,
}

impl UnitPair {
    pub fn symbol(&self, system: UnitSystem) -> &'static [u8] {
        match system {
            UnitSystem::Metric => self.metric,
            UnitSystem::Imperial => self.imperial,
        }
    }
}

static UNIT_PAIRS: [UnitPair; 5] = [
    UnitPair {
        metric: b"km/h\0",
        imperial: b"mph\0",
        scale: Scale::Ratio(KMH_PER_MPH),
    },
    UnitPair {
        metric: b"km\0",
        imperial: b"mi\0",
        scale: Scale::Ratio(KM_PER_MI),
    },
    UnitPair {
        metric: b"kPa\0",
        imperial: b"psi\0",
        scale: Scale::Ratio(KPA_PER_PSI),
    },
    UnitPair {
        metric: b"bar\0",
        imperial: b"psi\0",
        scale: Scale::Ratio(BAR_PER_PSI),
    },
    UnitPair {
        metric: "°C\0".as_bytes(),
        imperial: "°F\0".as_bytes(),
        scale: Scale::Fahrenheit,
    },
];

const ALIASES: [(&[u8], &[u8]); 5] = [
    (b"kph", b"km/h"),
    (b"km/hr", b"km/h"),
    (b"degc", b"c"),
    (b"degf", b"f"),
    (b"miles", b"mi"),
];

const NORM_CAP: usize = 8;
const DEGREE_LEAD: u8 = 0xC2;
const DEGREE_TRAIL: u8 = 0xB0;

struct Norm {
    buf: [u8; NORM_CAP],
    len: usize,
}

impl Norm {
    fn as_slice(&self) -> &[u8] {
        &self.buf[..self.len]
    }

    fn push(&mut self, byte: u8) -> bool {
        if self.len == NORM_CAP {
            return false;
        }
        self.buf[self.len] = byte.to_ascii_lowercase();
        self.len += 1;
        true
    }
}

fn normalise(unit: &[u8]) -> Option<Norm> {
    let mut norm = Norm {
        buf: [0; NORM_CAP],
        len: 0,
    };
    let mut i = 0;
    while i < unit.len() {
        let byte = unit[i];
        let degree = byte == DEGREE_LEAD && unit.get(i + 1) == Some(&DEGREE_TRAIL);
        i += if degree {
            2
        } else {
            1
        };
        if byte == 0 {
            break;
        }
        if degree || byte.is_ascii_whitespace() {
            continue;
        }
        if !norm.push(byte) {
            return None;
        }
    }
    Some(norm)
}

fn dealias(key: &[u8]) -> &[u8] {
    let mut i = 0;
    while i < ALIASES.len() {
        if ALIASES[i].0 == key {
            return ALIASES[i].1;
        }
        i += 1;
    }
    key
}

fn symbol_matches(symbol: &'static [u8], key: &[u8]) -> bool {
    match normalise(symbol) {
        Some(norm) => norm.as_slice() == key,
        None => false,
    }
}

fn resolve(unit: &[u8]) -> Option<(&'static UnitPair, UnitSystem)> {
    let norm = normalise(unit)?;
    let key = dealias(norm.as_slice());
    let mut i = 0;
    while i < UNIT_PAIRS.len() {
        let pair = &UNIT_PAIRS[i];
        if symbol_matches(pair.metric, key) {
            return Some((pair, UnitSystem::Metric));
        }
        if symbol_matches(pair.imperial, key) {
            return Some((pair, UnitSystem::Imperial));
        }
        i += 1;
    }
    None
}

pub fn display_symbol(unit: &[u8], system: UnitSystem) -> Option<&'static [u8]> {
    let (pair, _) = resolve(unit)?;
    Some(pair.symbol(system))
}

pub fn display_value(value: f32, unit: &[u8], system: UnitSystem) -> f32 {
    let Some((pair, declared)) = resolve(unit) else {
        return value;
    };
    if declared == system {
        return value;
    }
    match system {
        UnitSystem::Imperial => pair.scale.to_imperial(value),
        UnitSystem::Metric => pair.scale.to_metric(value),
    }
}

#[cfg(test)]
mod tests {
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
    fn from_u8_defaults_anything_unknown_to_metric() {
        assert_eq!(
            UnitSystem::from_u8(UNIT_SYSTEM_IMPERIAL),
            UnitSystem::Imperial
        );
        assert_eq!(UnitSystem::from_u8(UNIT_SYSTEM_METRIC), UnitSystem::Metric);
        assert_eq!(UnitSystem::from_u8(7), UnitSystem::Metric);
    }
}
