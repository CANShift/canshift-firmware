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

pub const LAP_CAPACITY: usize = 32;

pub const RUN_STATE_RESET: u8 = 0;
pub const RUN_STATE_RUNNING: u8 = 1;
pub const RUN_STATE_PAUSED: u8 = 2;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct TimerLap {
    pub lap_ms: u32,
    pub total_ms: u32,
    pub index: u16,
    pub session: u16,
}

#[repr(C)]
pub struct TimerCore {
    pub last_start_us: i64,
    pub accumulated_us: i64,
    pub last_lap_total_us: i64,
    pub version: u32,
    pub lap_count: u16,
    pub session: u16,
    pub run_state: u8,
    pub pend_head: u8,
    pub pend_count: u8,
    pub _pad: u8,
    pub pending: [TimerLap; LAP_CAPACITY],
}

pub struct LapOutcome {
    pub lap: TimerLap,
    pub dropped_oldest: bool,
}

impl TimerCore {
    pub const fn new() -> Self {
        Self {
            last_start_us: 0,
            accumulated_us: 0,
            last_lap_total_us: 0,
            version: 0,
            lap_count: 0,
            session: 0,
            run_state: RUN_STATE_RESET,
            pend_head: 0,
            pend_count: 0,
            _pad: 0,
            pending: [TimerLap {
                lap_ms: 0,
                total_ms: 0,
                index: 0,
                session: 0,
            }; LAP_CAPACITY],
        }
    }

    pub fn init(&mut self) {
        *self = Self::new();
    }

    pub fn start(&mut self, now_us: i64) -> bool {
        if self.run_state != RUN_STATE_RESET {
            return false;
        }
        self.last_start_us = now_us;
        self.accumulated_us = 0;
        self.last_lap_total_us = 0;
        self.lap_count = 0;
        self.session = self.session.wrapping_add(1);
        self.run_state = RUN_STATE_RUNNING;
        self.bump_version();
        true
    }

    pub fn pause(&mut self, now_us: i64) -> bool {
        if self.run_state != RUN_STATE_RUNNING {
            return false;
        }
        self.accumulated_us += now_us.saturating_sub(self.last_start_us);
        self.run_state = RUN_STATE_PAUSED;
        self.bump_version();
        true
    }

    pub fn resume(&mut self, now_us: i64) -> bool {
        if self.run_state != RUN_STATE_PAUSED {
            return false;
        }
        self.last_start_us = now_us;
        self.run_state = RUN_STATE_RUNNING;
        self.bump_version();
        true
    }

    pub fn reset(&mut self) -> bool {
        if self.run_state == RUN_STATE_RESET {
            return false;
        }
        self.run_state = RUN_STATE_RESET;
        self.last_start_us = 0;
        self.accumulated_us = 0;
        self.last_lap_total_us = 0;
        self.lap_count = 0;
        self.bump_version();
        true
    }

    pub fn lap(&mut self, now_us: i64) -> Option<LapOutcome> {
        if self.run_state != RUN_STATE_RUNNING {
            return None;
        }
        if self.lap_count == u16::MAX {
            return None;
        }
        let total_us = self.elapsed_us(now_us);
        let lap_us = total_us.saturating_sub(self.last_lap_total_us);
        self.last_lap_total_us = total_us;
        self.lap_count += 1;

        let lap = TimerLap {
            lap_ms: us_to_ms(lap_us),
            total_ms: us_to_ms(total_us),
            index: self.lap_count,
            session: self.session,
        };
        let dropped_oldest = self.push_pending(lap);
        self.bump_version();
        Some(LapOutcome {
            lap,
            dropped_oldest,
        })
    }

    pub fn elapsed_ms(&self, now_us: i64) -> u32 {
        us_to_ms(self.elapsed_us(now_us))
    }

    pub fn pending_count(&self) -> u8 {
        self.pend_count
    }

    pub fn pop_pending(&mut self) -> Option<TimerLap> {
        if self.pend_count == 0 {
            return None;
        }
        let lap = self.pending[self.pend_head as usize];
        self.pend_head = (self.pend_head + 1) % (LAP_CAPACITY as u8);
        self.pend_count -= 1;
        Some(lap)
    }

    fn elapsed_us(&self, now_us: i64) -> i64 {
        let running_us = if self.run_state == RUN_STATE_RUNNING {
            now_us.saturating_sub(self.last_start_us)
        } else {
            0
        };
        self.accumulated_us.saturating_add(running_us).max(0)
    }

    fn push_pending(&mut self, lap: TimerLap) -> bool {
        let capacity = LAP_CAPACITY as u8;
        let dropped = self.pend_count == capacity;
        if dropped {
            self.pend_head = (self.pend_head + 1) % capacity;
            self.pend_count -= 1;
        }
        let slot = (self.pend_head + self.pend_count) % capacity;
        self.pending[slot as usize] = lap;
        self.pend_count += 1;
        dropped
    }

    fn bump_version(&mut self) {
        self.version = self.version.wrapping_add(1);
    }
}

impl Default for TimerCore {
    fn default() -> Self {
        Self::new()
    }
}

fn us_to_ms(us: i64) -> u32 {
    let clamped = us.max(0) / 1000;
    if clamped > u32::MAX as i64 {
        u32::MAX
    } else {
        clamped as u32
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SEC: i64 = 1_000_000;

    #[test]
    fn struct_layout_matches_cxx_header() {
        assert_eq!(core::mem::size_of::<TimerLap>(), 12);
        assert_eq!(core::mem::size_of::<TimerCore>(), 424);
        assert_eq!(core::mem::align_of::<TimerCore>(), 8);
    }

    #[test]
    fn starts_only_from_reset() {
        let mut t = TimerCore::new();
        assert!(t.start(0));
        assert!(!t.start(SEC));
        assert_eq!(t.run_state, RUN_STATE_RUNNING);
        assert_eq!(t.session, 1);
    }

    #[test]
    fn elapsed_accumulates_across_pause_resume() {
        let mut t = TimerCore::new();
        t.start(0);
        assert_eq!(t.elapsed_ms(5 * SEC), 5_000);
        assert!(t.pause(5 * SEC));
        assert_eq!(t.elapsed_ms(60 * SEC), 5_000);
        assert!(t.resume(60 * SEC));
        assert_eq!(t.elapsed_ms(62 * SEC), 7_000);
    }

    #[test]
    fn pause_requires_running_and_resume_requires_paused() {
        let mut t = TimerCore::new();
        assert!(!t.pause(0));
        assert!(!t.resume(0));
        t.start(0);
        assert!(!t.resume(SEC));
        t.pause(SEC);
        assert!(!t.pause(2 * SEC));
    }

    #[test]
    fn reset_clears_run_state_but_keeps_pending_laps() {
        let mut t = TimerCore::new();
        t.start(0);
        t.lap(10 * SEC);
        assert!(t.reset());
        assert_eq!(t.run_state, RUN_STATE_RESET);
        assert_eq!(t.lap_count, 0);
        assert_eq!(t.elapsed_ms(99 * SEC), 0);
        assert_eq!(t.pending_count(), 1);
        assert!(!t.reset());
    }

    #[test]
    fn lap_records_split_and_total() {
        let mut t = TimerCore::new();
        t.start(0);
        let first = t.lap(61 * SEC).unwrap();
        assert_eq!(first.lap.index, 1);
        assert_eq!(first.lap.lap_ms, 61_000);
        assert_eq!(first.lap.total_ms, 61_000);
        assert!(!first.dropped_oldest);

        let second = t.lap(100 * SEC).unwrap();
        assert_eq!(second.lap.index, 2);
        assert_eq!(second.lap.lap_ms, 39_000);
        assert_eq!(second.lap.total_ms, 100_000);
    }

    #[test]
    fn lap_split_spans_pause_correctly() {
        let mut t = TimerCore::new();
        t.start(0);
        t.pause(10 * SEC);
        t.resume(50 * SEC);
        let lap = t.lap(55 * SEC).unwrap();
        assert_eq!(lap.lap.lap_ms, 15_000);
        assert_eq!(lap.lap.total_ms, 15_000);
    }

    #[test]
    fn lap_ignored_unless_running() {
        let mut t = TimerCore::new();
        assert!(t.lap(0).is_none());
        t.start(0);
        t.pause(SEC);
        assert!(t.lap(2 * SEC).is_none());
    }

    #[test]
    fn pending_ring_flushes_in_order() {
        let mut t = TimerCore::new();
        t.start(0);
        for i in 1..=3 {
            t.lap(i * SEC);
        }
        let popped: Vec<u16> = core::iter::from_fn(|| t.pop_pending())
            .map(|lap| lap.index)
            .collect();
        assert_eq!(popped, vec![1, 2, 3]);
        assert_eq!(t.pending_count(), 0);
        assert!(t.pop_pending().is_none());
    }

    #[test]
    fn pending_ring_drops_oldest_on_overflow() {
        let mut t = TimerCore::new();
        t.start(0);
        let mut dropped_seen = false;
        for i in 1..=(LAP_CAPACITY as i64 + 2) {
            let outcome = t.lap(i * SEC).unwrap();
            dropped_seen |= outcome.dropped_oldest;
        }
        assert!(dropped_seen);
        assert_eq!(t.pending_count() as usize, LAP_CAPACITY);
        let first = t.pop_pending().unwrap();
        assert_eq!(first.index, 3);
        assert_eq!(t.lap_count as usize, LAP_CAPACITY + 2);
    }

    #[test]
    fn session_increments_on_each_start() {
        let mut t = TimerCore::new();
        t.start(0);
        t.lap(SEC);
        t.reset();
        t.start(10 * SEC);
        assert_eq!(t.session, 2);
        let lap = t.lap(11 * SEC).unwrap();
        assert_eq!(lap.lap.session, 2);
        assert_eq!(lap.lap.index, 1);
    }

    #[test]
    fn version_bumps_on_every_mutation() {
        let mut t = TimerCore::new();
        let mut last = t.version;
        t.start(0);
        assert_ne!(t.version, last);
        last = t.version;
        t.lap(SEC);
        assert_ne!(t.version, last);
        last = t.version;
        t.pause(2 * SEC);
        assert_ne!(t.version, last);
        last = t.version;
        t.resume(3 * SEC);
        assert_ne!(t.version, last);
        last = t.version;
        t.reset();
        assert_ne!(t.version, last);
    }

    #[test]
    fn rejected_transitions_do_not_bump_version() {
        let mut t = TimerCore::new();
        let before = t.version;
        t.pause(0);
        t.resume(0);
        t.reset();
        t.lap(0);
        assert_eq!(t.version, before);
    }

    #[test]
    fn elapsed_is_robust_against_clock_anomalies() {
        let mut t = TimerCore::new();
        t.start(100 * SEC);
        assert_eq!(t.elapsed_ms(99 * SEC), 0);
    }
}
