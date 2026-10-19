#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

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
    pub last_lap_ms: u32,
    pub best_lap_ms: u32,
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
            last_lap_ms: 0,
            best_lap_ms: 0,
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
        self.clear_splits();
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
        self.clear_splits();
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
        self.record_split(us_to_ms(lap_us));

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

    pub fn current_lap_ms(&self, now_us: i64) -> u32 {
        us_to_ms(
            self.elapsed_us(now_us)
                .saturating_sub(self.last_lap_total_us),
        )
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

    fn clear_splits(&mut self) {
        self.last_lap_total_us = 0;
        self.last_lap_ms = 0;
        self.best_lap_ms = 0;
        self.lap_count = 0;
    }

    fn record_split(&mut self, lap_ms: u32) {
        self.last_lap_ms = lap_ms;
        if self.best_lap_ms == 0 || lap_ms < self.best_lap_ms {
            self.best_lap_ms = lap_ms;
        }
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
mod tests;
