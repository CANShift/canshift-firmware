use crate::{ids, SignalId, SIGNAL_COUNT};

pub const DISPLAY_TAU_MS: u32 = 250;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DisplayClass {
    Decoded,
    Smoothed,
}

const SMOOTHED_SIGNALS: [SignalId; 10] = [
    ids::IAT_C,
    ids::COOLANT_TEMP_C,
    ids::OIL_TEMP_C,
    ids::EGT_C,
    ids::GEARBOX_TEMP_C,
    ids::DIFF_TEMP_C,
    ids::OIL_PRESS_BAR,
    ids::FUEL_PRESS_BAR,
    ids::BATTERY_VOLTS,
    ids::FUEL_LEVEL_PCT,
];

const DISPLAY_CLASSES: [DisplayClass; SIGNAL_COUNT as usize] = build_classes();

const fn build_classes() -> [DisplayClass; SIGNAL_COUNT as usize] {
    let mut classes = [DisplayClass::Decoded; SIGNAL_COUNT as usize];
    let mut i = 0;
    while i < SMOOTHED_SIGNALS.len() {
        classes[SMOOTHED_SIGNALS[i] as usize] = DisplayClass::Smoothed;
        i += 1;
    }
    classes
}

pub fn display_class(id: SignalId) -> DisplayClass {
    DISPLAY_CLASSES
        .get(id as usize)
        .copied()
        .unwrap_or(DisplayClass::Decoded)
}

pub fn display_step(id: SignalId, previous: f32, sample: f32, dt_ms: u32, seeded: bool) -> f32 {
    if !seeded || display_class(id) == DisplayClass::Decoded {
        return sample;
    }
    let dt = dt_ms as f32;
    let alpha = dt / (DISPLAY_TAU_MS as f32 + dt);
    previous + alpha * (sample - previous)
}

#[cfg(test)]
mod tests {
    use super::*;

    const FRAME_10HZ_MS: u32 = 100;
    const FRAME_50HZ_MS: u32 = 20;

    fn settle(id: SignalId, from: f32, to: f32, frame_ms: u32, window_ms: u32) -> f32 {
        let mut value = from;
        for _ in 0..(window_ms / frame_ms) {
            value = display_step(id, value, to, frame_ms, true);
        }
        value
    }

    #[test]
    fn fast_signals_display_the_decoded_sample() {
        for id in [
            ids::RPM,
            ids::SPEED_KPH,
            ids::THROTTLE_POS,
            ids::LAMBDA_1,
            ids::AFR_1,
            ids::BOOST_BAR,
            ids::MAP_KPA,
            ids::GEAR,
            ids::FLAG_MIL,
        ] {
            assert_eq!(display_class(id), DisplayClass::Decoded);
            assert_eq!(
                display_step(id, 1000.0, 7000.0, FRAME_10HZ_MS, true),
                7000.0
            );
        }
    }

    #[test]
    fn slow_signals_are_smoothed() {
        for id in SMOOTHED_SIGNALS {
            assert_eq!(display_class(id), DisplayClass::Smoothed);
        }
        let step = display_step(ids::COOLANT_TEMP_C, 90.0, 100.0, FRAME_10HZ_MS, true);
        assert!(step > 90.0 && step < 100.0);
    }

    #[test]
    fn first_sample_seeds_the_display_value() {
        assert_eq!(
            display_step(ids::OIL_PRESS_BAR, 0.0, 4.0, FRAME_10HZ_MS, false),
            4.0
        );
    }

    #[test]
    fn lag_depends_on_time_not_on_frame_rate() {
        let at_10hz = settle(ids::OIL_PRESS_BAR, 4.0, 0.0, FRAME_10HZ_MS, 1000);
        let at_50hz = settle(ids::OIL_PRESS_BAR, 4.0, 0.0, FRAME_50HZ_MS, 1000);
        assert!(at_10hz < 0.2);
        assert!(at_50hz < 0.2);
        assert!((at_10hz - at_50hz).abs() < 0.1);
    }

    #[test]
    fn a_long_gap_jumps_to_the_sample() {
        let value = display_step(ids::BATTERY_VOLTS, 12.0, 14.0, 60_000, true);
        assert!((value - 14.0).abs() < 0.01);
    }

    #[test]
    fn out_of_range_ids_are_decoded() {
        assert_eq!(display_class(SIGNAL_COUNT), DisplayClass::Decoded);
        assert_eq!(
            display_step(SignalId::MAX, 1.0, 2.0, FRAME_10HZ_MS, true),
            2.0
        );
    }
}
