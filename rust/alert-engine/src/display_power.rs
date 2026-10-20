use crate::{display_idle_state, DISPLAY_IDLE_LIVE, DISPLAY_IDLE_OFF};

pub const TOUCH_READ_ACTIVE_MS: u32 = 15;
pub const TOUCH_READ_IDLE_MS: u32 = 33;
pub const TOUCH_READ_PANEL_OFF_MS: u32 = 100;
pub const TOUCH_IDLE_AFTER_MS: u32 = 3_000;

const _: () = assert!(TOUCH_READ_IDLE_MS > TOUCH_READ_ACTIVE_MS);
const _: () = assert!(TOUCH_READ_PANEL_OFF_MS > TOUCH_READ_IDLE_MS);

#[must_use]
pub fn held_display_idle_state(
    idle_ms: u32,
    dim_after_ms: u32,
    off_after_ms: u32,
    alert_held: bool,
) -> u8 {
    if alert_held {
        return DISPLAY_IDLE_LIVE;
    }
    display_idle_state(idle_ms, dim_after_ms, off_after_ms)
}

#[must_use]
pub const fn touch_read_period_ms(display_state: u8, ms_since_touch: u32) -> u32 {
    if display_state == DISPLAY_IDLE_OFF {
        return TOUCH_READ_PANEL_OFF_MS;
    }
    if ms_since_touch >= TOUCH_IDLE_AFTER_MS {
        return TOUCH_READ_IDLE_MS;
    }
    TOUCH_READ_ACTIVE_MS
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::DISPLAY_IDLE_DIM;

    const DIM_AFTER_MS: u32 = 60_000;
    const OFF_AFTER_MS: u32 = 600_000;

    #[test]
    fn a_held_alert_keeps_the_panel_live() {
        assert_eq!(
            held_display_idle_state(u32::MAX, DIM_AFTER_MS, OFF_AFTER_MS, true),
            DISPLAY_IDLE_LIVE
        );
    }

    #[test]
    fn without_an_alert_the_idle_ladder_applies() {
        assert_eq!(
            held_display_idle_state(OFF_AFTER_MS, DIM_AFTER_MS, OFF_AFTER_MS, false),
            DISPLAY_IDLE_OFF
        );
        assert_eq!(
            held_display_idle_state(DIM_AFTER_MS, DIM_AFTER_MS, OFF_AFTER_MS, false),
            DISPLAY_IDLE_DIM
        );
    }

    #[test]
    fn a_recent_touch_reads_at_the_full_rate() {
        assert_eq!(
            touch_read_period_ms(DISPLAY_IDLE_LIVE, 0),
            TOUCH_READ_ACTIVE_MS
        );
        assert_eq!(
            touch_read_period_ms(DISPLAY_IDLE_LIVE, TOUCH_IDLE_AFTER_MS - 1),
            TOUCH_READ_ACTIVE_MS
        );
    }

    #[test]
    fn an_untouched_live_panel_reads_slower() {
        assert_eq!(
            touch_read_period_ms(DISPLAY_IDLE_LIVE, TOUCH_IDLE_AFTER_MS),
            TOUCH_READ_IDLE_MS
        );
        assert_eq!(
            touch_read_period_ms(DISPLAY_IDLE_DIM, u32::MAX),
            TOUCH_READ_IDLE_MS
        );
    }

    #[test]
    fn a_dark_panel_reads_slowest_whatever_the_touch_age() {
        assert_eq!(
            touch_read_period_ms(DISPLAY_IDLE_OFF, 0),
            TOUCH_READ_PANEL_OFF_MS
        );
    }
}
