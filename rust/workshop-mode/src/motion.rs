pub const WORKSHOP_MOTION_EXIT_KPH: f32 = 5.0;
pub const WORKSHOP_MOTION_EXIT_MS: u32 = 3000;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct MotionWatch {
    pub moving_since_ms: u32,
    pub moving: bool,
}

#[must_use]
pub fn moving(speed_valid: bool, speed_kph: f32) -> bool {
    speed_valid && speed_kph.abs() >= WORKSHOP_MOTION_EXIT_KPH
}

#[must_use]
pub fn motion_exit_due(
    watch: &mut MotionWatch,
    speed_valid: bool,
    speed_kph: f32,
    now_ms: u32,
) -> bool {
    if !moving(speed_valid, speed_kph) {
        *watch = MotionWatch::default();
        return false;
    }
    if !watch.moving {
        *watch = MotionWatch {
            moving_since_ms: now_ms,
            moving: true,
        };
    }
    now_ms.wrapping_sub(watch.moving_since_ms) >= WORKSHOP_MOTION_EXIT_MS
}

#[cfg(test)]
mod tests {
    use super::*;

    fn drive(watch: &mut MotionWatch, speed_kph: f32, from_ms: u32, until_ms: u32) -> bool {
        (from_ms..=until_ms)
            .step_by(100)
            .map(|now_ms| motion_exit_due(watch, true, speed_kph, now_ms))
            .last()
            .unwrap_or(false)
    }

    #[test]
    fn sustained_motion_is_due_after_the_window() {
        let mut watch = MotionWatch::default();
        assert!(!drive(
            &mut watch,
            20.0,
            1000,
            1000 + WORKSHOP_MOTION_EXIT_MS - 100
        ));
        assert!(motion_exit_due(
            &mut watch,
            true,
            20.0,
            1000 + WORKSHOP_MOTION_EXIT_MS
        ));
    }

    #[test]
    fn stopping_restarts_the_window() {
        let mut watch = MotionWatch::default();
        assert!(!drive(&mut watch, 20.0, 1000, 2000));
        assert!(!motion_exit_due(&mut watch, true, 0.0, 2100));
        assert!(!drive(
            &mut watch,
            20.0,
            2200,
            2200 + WORKSHOP_MOTION_EXIT_MS - 100
        ));
    }

    #[test]
    fn a_crawl_below_the_threshold_is_never_due() {
        let mut watch = MotionWatch::default();
        assert!(!drive(&mut watch, 4.9, 0, 4 * WORKSHOP_MOTION_EXIT_MS));
    }

    #[test]
    fn reversing_counts_as_motion() {
        let mut watch = MotionWatch::default();
        assert!(drive(&mut watch, -8.0, 0, WORKSHOP_MOTION_EXIT_MS));
    }

    #[test]
    fn a_stale_or_nan_speed_is_never_due() {
        let mut watch = MotionWatch::default();
        assert!(!motion_exit_due(&mut watch, false, 80.0, 0));
        assert!(!motion_exit_due(
            &mut watch,
            false,
            80.0,
            WORKSHOP_MOTION_EXIT_MS
        ));
        assert!(!motion_exit_due(&mut watch, true, f32::NAN, 0));
    }

    #[test]
    fn the_window_survives_a_millis_wrap() {
        let mut watch = MotionWatch::default();
        let start = u32::MAX - 1000;
        assert!(!motion_exit_due(&mut watch, true, 20.0, start));
        assert!(motion_exit_due(
            &mut watch,
            true,
            20.0,
            start.wrapping_add(WORKSHOP_MOTION_EXIT_MS)
        ));
    }
}
