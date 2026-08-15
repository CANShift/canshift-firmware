pub const NEVER_RECEIVED_MS: u32 = u32::MAX;
pub const MS_PER_SECOND: u32 = 1000;

pub const STALE_DASH_GROUPS_MAX: u8 = 4;
pub const STALE_DASH_GROUPS_DEFAULT: u8 = 2;
pub const WIDE_VALUE_DIGITS: u8 = 4;

#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct BusSilence {
    pub silent: bool,
    pub seconds: u32,
}

#[must_use]
pub fn bus_silence(ms_since_rx: u32, uptime_ms: u32, threshold_ms: u32) -> BusSilence {
    let silent_ms = if ms_since_rx == NEVER_RECEIVED_MS {
        uptime_ms
    } else {
        ms_since_rx
    };
    BusSilence {
        silent: silent_ms >= threshold_ms,
        seconds: silent_ms / MS_PER_SECOND,
    }
}

const DIGIT_STEPS: [(f32, u8); 4] = [(10_000.0, 5), (1_000.0, 4), (100.0, 3), (10.0, 2)];

#[must_use]
pub fn int_digits(max_value: f32) -> u8 {
    if !max_value.is_finite() {
        return 1;
    }
    let magnitude = if max_value < 0.0 {
        -max_value
    } else {
        max_value
    };
    for (limit, digits) in DIGIT_STEPS {
        if magnitude >= limit {
            return digits;
        }
    }
    1
}

#[must_use]
pub fn stale_dash_groups(max_value: f32) -> u8 {
    if int_digits(max_value) >= WIDE_VALUE_DIGITS {
        STALE_DASH_GROUPS_MAX
    } else {
        STALE_DASH_GROUPS_DEFAULT
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const THRESHOLD_MS: u32 = 500;

    #[test]
    fn live_bus_is_not_silent() {
        let s = bus_silence(499, 60_000, THRESHOLD_MS);
        assert!(!s.silent);
        assert_eq!(s.seconds, 0);
    }

    #[test]
    fn threshold_is_inclusive() {
        assert!(bus_silence(THRESHOLD_MS, 60_000, THRESHOLD_MS).silent);
    }

    #[test]
    fn seconds_floor_to_whole_seconds() {
        assert_eq!(bus_silence(4_999, 60_000, THRESHOLD_MS).seconds, 4);
        assert_eq!(bus_silence(5_000, 60_000, THRESHOLD_MS).seconds, 5);
    }

    #[test]
    fn never_received_counts_from_uptime() {
        let s = bus_silence(NEVER_RECEIVED_MS, 4_200, THRESHOLD_MS);
        assert!(s.silent);
        assert_eq!(s.seconds, 4);
    }

    #[test]
    fn never_received_before_threshold_is_not_silent() {
        assert!(!bus_silence(NEVER_RECEIVED_MS, 120, THRESHOLD_MS).silent);
    }

    #[test]
    fn digits_count_the_integer_part() {
        assert_eq!(int_digits(0.0), 1);
        assert_eq!(int_digits(9.9), 1);
        assert_eq!(int_digits(10.0), 2);
        assert_eq!(int_digits(300.0), 3);
        assert_eq!(int_digits(7_200.0), 4);
        assert_eq!(int_digits(10_000.0), 5);
    }

    #[test]
    fn digits_ignore_sign_and_reject_nan() {
        assert_eq!(int_digits(-7_200.0), 4);
        assert_eq!(int_digits(f32::NAN), 1);
        assert_eq!(int_digits(f32::INFINITY), 1);
    }

    #[test]
    fn only_four_digit_values_widen_the_placeholder() {
        assert_eq!(stale_dash_groups(300.0), STALE_DASH_GROUPS_DEFAULT);
        assert_eq!(stale_dash_groups(16.0), STALE_DASH_GROUPS_DEFAULT);
        assert_eq!(stale_dash_groups(7_200.0), STALE_DASH_GROUPS_MAX);
        assert_eq!(stale_dash_groups(12_000.0), STALE_DASH_GROUPS_MAX);
    }
}
