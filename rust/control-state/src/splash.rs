pub const CONTROL_SPLASH_CHANGE_MS: u32 = 800;
pub const CONTROL_SPLASH_REFUSAL_MS: u32 = 1200;
pub const CONTROL_SPLASH_KIND_COUNT: u8 = 2;

const HOLD_MS: [u32; CONTROL_SPLASH_KIND_COUNT as usize] =
    [CONTROL_SPLASH_CHANGE_MS, CONTROL_SPLASH_REFUSAL_MS];

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum SplashKind {
    #[default]
    Change = 0,
    Refusal = 1,
}

impl SplashKind {
    #[must_use]
    pub fn from_raw(raw: u8) -> Self {
        if raw == SplashKind::Refusal as u8 {
            SplashKind::Refusal
        } else {
            SplashKind::Change
        }
    }

    #[must_use]
    pub fn hold_ms(self) -> u32 {
        HOLD_MS[self as usize]
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct SplashTimer {
    pub started_ms: u32,
    pub hold_ms: u32,
    pub showing: bool,
    pub _pad: [u8; 3],
}

impl SplashTimer {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn raise(&mut self, kind: SplashKind, now_ms: u32) {
        self.started_ms = now_ms;
        self.hold_ms = kind.hold_ms();
        self.showing = true;
    }

    pub fn preempt(&mut self) -> bool {
        let was_showing = self.showing;
        self.showing = false;
        was_showing
    }

    pub fn poll(&mut self, now_ms: u32) -> bool {
        if !self.showing {
            return false;
        }
        if now_ms.wrapping_sub(self.started_ms) >= self.hold_ms {
            self.showing = false;
            return false;
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_change_holds_800_ms_and_a_refusal_1200() {
        assert_eq!(SplashKind::Change.hold_ms(), 800);
        assert_eq!(SplashKind::Refusal.hold_ms(), 1_200);
    }

    #[test]
    fn a_change_shows_until_its_last_millisecond() {
        let mut timer = SplashTimer::new();
        timer.raise(SplashKind::Change, 1_000);
        assert!(timer.poll(1_000));
        assert!(timer.poll(1_799));
        assert!(!timer.poll(1_800));
        assert!(!timer.poll(1_801));
    }

    #[test]
    fn a_refusal_holds_the_longer_window() {
        let mut timer = SplashTimer::new();
        timer.raise(SplashKind::Refusal, 0);
        assert!(timer.poll(1_199));
        assert!(!timer.poll(1_200));
    }

    #[test]
    fn a_second_change_replaces_the_first_and_restarts_the_window() {
        let mut timer = SplashTimer::new();
        timer.raise(SplashKind::Change, 0);
        assert!(timer.poll(700));
        timer.raise(SplashKind::Change, 700);
        assert!(timer.poll(1_400));
        assert!(!timer.poll(1_500));
    }

    #[test]
    fn a_change_inside_a_refusal_shortens_the_window_to_its_own() {
        let mut timer = SplashTimer::new();
        timer.raise(SplashKind::Refusal, 0);
        timer.raise(SplashKind::Change, 100);
        assert!(timer.poll(899));
        assert!(!timer.poll(900));
    }

    #[test]
    fn preempt_hides_at_once_and_reports_whether_it_was_showing() {
        let mut timer = SplashTimer::new();
        timer.raise(SplashKind::Change, 0);
        assert!(timer.preempt());
        assert!(!timer.poll(1));
        assert!(!timer.preempt());
    }

    #[test]
    fn a_window_opened_before_the_millis_wrap_still_closes() {
        let mut timer = SplashTimer::new();
        let start = u32::MAX - 100;
        timer.raise(SplashKind::Change, start);
        assert!(timer.poll(start.wrapping_add(799)));
        assert!(!timer.poll(start.wrapping_add(800)));
    }

    #[test]
    fn an_unraised_timer_never_shows() {
        let mut timer = SplashTimer::new();
        assert!(!timer.poll(0));
        assert!(!timer.poll(u32::MAX));
    }

    #[test]
    fn kind_from_raw_falls_back_to_a_change() {
        assert_eq!(SplashKind::from_raw(0), SplashKind::Change);
        assert_eq!(SplashKind::from_raw(1), SplashKind::Refusal);
        assert_eq!(SplashKind::from_raw(9), SplashKind::Change);
    }
}
