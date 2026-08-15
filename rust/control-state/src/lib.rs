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

pub const CONTROL_STATE_COUNT: u8 = 4;
pub const CONTROL_STEP_MAX: u8 = 6;
pub const CONTROL_LONG_PRESS_MS: u32 = 600;
pub const CONTROL_STEP_OFF: u8 = 0;
pub const CONTROL_STEP_FIRST: u8 = 1;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum ControlState {
    #[default]
    Off = 0,
    Armed = 1,
    Active = 2,
    Unavailable = 3,
}

impl ControlState {
    #[inline]
    #[must_use]
    pub fn as_u8(self) -> u8 {
        self as u8
    }
}

#[must_use]
pub fn resolve(blocked: bool, acting: bool, requested: bool) -> ControlState {
    if blocked {
        return ControlState::Unavailable;
    }
    if acting {
        return ControlState::Active;
    }
    if requested {
        return ControlState::Armed;
    }
    ControlState::Off
}

#[must_use]
pub fn step_tap(level: u8) -> u8 {
    if level >= CONTROL_STEP_MAX {
        return CONTROL_STEP_OFF;
    }
    level + 1
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Stepper {
    pub press_start_ms: u32,
    pub level: u8,
    pub pressed: bool,
    pub long_fired: bool,
    pub _pad: u8,
}

impl Stepper {
    #[must_use]
    pub fn new(level: u8) -> Self {
        Self {
            press_start_ms: 0,
            level: level.min(CONTROL_STEP_MAX),
            pressed: false,
            long_fired: false,
            _pad: 0,
        }
    }

    pub fn press(&mut self, now_ms: u32) {
        self.press_start_ms = now_ms;
        self.pressed = true;
        self.long_fired = false;
    }

    pub fn poll(&mut self, now_ms: u32) -> bool {
        if !self.pressed || self.long_fired {
            return false;
        }
        if now_ms.wrapping_sub(self.press_start_ms) < CONTROL_LONG_PRESS_MS {
            return false;
        }
        self.level = CONTROL_STEP_FIRST;
        self.long_fired = true;
        true
    }

    pub fn release(&mut self, now_ms: u32) -> bool {
        if !self.pressed {
            return false;
        }
        let long = self.long_fired || self.poll(now_ms);
        self.pressed = false;
        self.long_fired = false;
        if long {
            return false;
        }
        self.level = step_tap(self.level);
        true
    }

    pub fn sync(&mut self, level: u8) -> bool {
        let clamped = level.min(CONTROL_STEP_MAX);
        if self.pressed || clamped == self.level {
            return false;
        }
        self.level = clamped;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolve_orders_blocked_over_acting_over_requested() {
        assert_eq!(resolve(true, true, true), ControlState::Unavailable);
        assert_eq!(resolve(false, true, true), ControlState::Active);
        assert_eq!(resolve(false, false, true), ControlState::Armed);
        assert_eq!(resolve(false, false, false), ControlState::Off);
    }

    #[test]
    fn state_discriminants_match_the_cpp_enum() {
        assert_eq!(ControlState::Off.as_u8(), 0);
        assert_eq!(ControlState::Armed.as_u8(), 1);
        assert_eq!(ControlState::Active.as_u8(), 2);
        assert_eq!(ControlState::Unavailable.as_u8(), 3);
    }

    #[test]
    fn tap_climbs_then_wraps_to_off() {
        let mut level = CONTROL_STEP_OFF;
        for expected in 1..=CONTROL_STEP_MAX {
            level = step_tap(level);
            assert_eq!(level, expected);
        }
        assert_eq!(step_tap(level), CONTROL_STEP_OFF);
    }

    #[test]
    fn short_press_steps_up_once() {
        let mut s = Stepper::new(3);
        s.press(1_000);
        assert!(!s.poll(1_100));
        assert!(s.release(1_200));
        assert_eq!(s.level, 4);
    }

    #[test]
    fn long_press_returns_to_level_one_without_a_tap() {
        let mut s = Stepper::new(5);
        s.press(1_000);
        assert!(!s.poll(1_599));
        assert!(s.poll(1_600));
        assert_eq!(s.level, CONTROL_STEP_FIRST);
        assert!(!s.poll(2_000));
        assert!(!s.release(2_000));
        assert_eq!(s.level, CONTROL_STEP_FIRST);
    }

    #[test]
    fn release_past_the_hold_never_also_steps() {
        let mut s = Stepper::new(2);
        s.press(0);
        assert!(!s.release(CONTROL_LONG_PRESS_MS));
        assert_eq!(s.level, CONTROL_STEP_FIRST);
    }

    #[test]
    fn release_without_a_press_is_a_noop() {
        let mut s = Stepper::new(2);
        assert!(!s.release(500));
        assert_eq!(s.level, 2);
    }

    #[test]
    fn press_start_wrap_still_measures_the_hold() {
        let mut s = Stepper::new(1);
        let start = u32::MAX - 100;
        s.press(start);
        assert!(!s.poll(start.wrapping_add(599)));
        assert!(s.poll(start.wrapping_add(600)));
    }

    #[test]
    fn sync_adopts_an_external_level_unless_a_finger_is_down() {
        let mut s = Stepper::new(1);
        assert!(s.sync(4));
        assert_eq!(s.level, 4);
        assert!(!s.sync(4));
        s.press(0);
        assert!(!s.sync(6));
        assert_eq!(s.level, 4);
    }

    #[test]
    fn sync_clamps_above_the_top_level() {
        let mut s = Stepper::new(0);
        assert!(s.sync(200));
        assert_eq!(s.level, CONTROL_STEP_MAX);
    }
}
