#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct LapWatch {
    pub lap_count: u16,
    pub timer_session: u16,
}

pub fn observe(watch: &mut LapWatch, lap_count: u16, timer_session: u16) -> bool {
    let restarted = timer_session != watch.timer_session || lap_count < watch.lap_count;
    let advanced = !restarted && lap_count > watch.lap_count;
    watch.lap_count = lap_count;
    watch.timer_session = timer_session;
    advanced
}

pub fn rebase(watch: &mut LapWatch, lap_count: u16, timer_session: u16) {
    watch.lap_count = lap_count;
    watch.timer_session = timer_session;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_new_lap_is_reported_once() {
        let mut watch = LapWatch::default();
        assert!(observe(&mut watch, 1, 0));
        assert!(!observe(&mut watch, 1, 0));
        assert!(observe(&mut watch, 2, 0));
    }

    #[test]
    fn a_timer_reset_is_not_a_lap() {
        let mut watch = LapWatch::default();
        rebase(&mut watch, 4, 3);
        assert!(!observe(&mut watch, 0, 4));
        assert!(observe(&mut watch, 1, 4));
    }

    #[test]
    fn laps_already_run_when_the_session_opens_are_not_replayed() {
        let mut watch = LapWatch::default();
        rebase(&mut watch, 6, 2);
        assert!(!observe(&mut watch, 6, 2));
        assert!(observe(&mut watch, 7, 2));
    }
}
