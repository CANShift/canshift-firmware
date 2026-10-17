pub const PAGE_BUTTON_DEBOUNCE_MS: u32 = 150;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct PageButtonEdge {
    pub last_press_ms: u32,
    pub released_seen: bool,
    pub has_pressed: bool,
    pub _pad: [u8; 2],
}

impl PageButtonEdge {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn sample(&mut self, valid: bool, raw: f32, now_ms: u32) -> bool {
        if !valid || raw.is_nan() {
            return false;
        }
        if raw == 0.0 {
            self.released_seen = true;
            return false;
        }
        if !self.released_seen {
            return false;
        }
        self.released_seen = false;
        self.accept_press(now_ms)
    }

    fn accept_press(&mut self, now_ms: u32) -> bool {
        if self.has_pressed && now_ms.wrapping_sub(self.last_press_ms) < PAGE_BUTTON_DEBOUNCE_MS {
            return false;
        }
        self.last_press_ms = now_ms;
        self.has_pressed = true;
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn released_then_pressed(edge: &mut PageButtonEdge, now_ms: u32) -> bool {
        assert!(!edge.sample(true, 0.0, now_ms));
        edge.sample(true, 1.0, now_ms)
    }

    #[test]
    fn a_rising_edge_from_zero_is_one_press() {
        let mut edge = PageButtonEdge::new();
        assert!(released_then_pressed(&mut edge, 1_000));
        assert!(!edge.sample(true, 1.0, 1_030));
        assert!(!edge.sample(true, 1.0, 2_000));
    }

    #[test]
    fn any_non_zero_value_counts_as_pressed() {
        let mut edge = PageButtonEdge::new();
        assert!(!edge.sample(true, 0.0, 0));
        assert!(edge.sample(true, -2.0, 0));
        assert!(!edge.sample(true, 0.0, 500));
        assert!(edge.sample(true, 0.25, 500));
    }

    #[test]
    fn a_signal_already_high_at_start_never_fires_until_released() {
        let mut edge = PageButtonEdge::new();
        assert!(!edge.sample(true, 1.0, 0));
        assert!(!edge.sample(true, 1.0, 500));
        assert!(released_then_pressed(&mut edge, 600));
    }

    #[test]
    fn a_bounce_inside_the_debounce_window_is_dropped() {
        let mut edge = PageButtonEdge::new();
        assert!(released_then_pressed(&mut edge, 1_000));
        assert!(!released_then_pressed(
            &mut edge,
            1_000 + PAGE_BUTTON_DEBOUNCE_MS - 1
        ));
        assert!(released_then_pressed(
            &mut edge,
            1_000 + PAGE_BUTTON_DEBOUNCE_MS
        ));
    }

    #[test]
    fn the_first_press_is_not_debounced_against_boot() {
        let mut edge = PageButtonEdge::new();
        assert!(released_then_pressed(&mut edge, 10));
    }

    #[test]
    fn a_stale_signal_does_nothing_while_stale() {
        let mut edge = PageButtonEdge::new();
        assert!(!edge.sample(true, 0.0, 0));
        assert!(!edge.sample(false, 1.0, 100));
        assert!(!edge.sample(false, 1.0, 5_000));
    }

    #[test]
    fn a_press_after_a_silent_gap_still_fires_when_last_seen_released() {
        let mut edge = PageButtonEdge::new();
        assert!(!edge.sample(true, 0.0, 0));
        assert!(!edge.sample(false, 0.0, 30_000));
        assert!(edge.sample(true, 1.0, 60_000));
    }

    #[test]
    fn a_button_held_through_a_silent_gap_does_not_fire_again() {
        let mut edge = PageButtonEdge::new();
        assert!(released_then_pressed(&mut edge, 0));
        assert!(!edge.sample(false, 1.0, 1_000));
        assert!(!edge.sample(true, 1.0, 2_000));
    }

    #[test]
    fn nan_reads_as_stale() {
        let mut edge = PageButtonEdge::new();
        assert!(!edge.sample(true, f32::NAN, 0));
        assert!(!edge.sample(true, 1.0, 100));
        assert!(!edge.sample(true, 0.0, 200));
        assert!(!edge.sample(true, f32::NAN, 300));
        assert!(edge.sample(true, 1.0, 400));
    }

    #[test]
    fn the_debounce_survives_the_millis_wrap() {
        let mut edge = PageButtonEdge::new();
        let start = u32::MAX - 50;
        assert!(released_then_pressed(&mut edge, start));
        assert!(!released_then_pressed(&mut edge, start.wrapping_add(100)));
        assert!(released_then_pressed(
            &mut edge,
            start.wrapping_add(PAGE_BUTTON_DEBOUNCE_MS)
        ));
    }
}
