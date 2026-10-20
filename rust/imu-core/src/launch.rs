use crate::vector::abs;

pub const LAUNCH_IDLE: u8 = 0;
pub const LAUNCH_ARMED: u8 = 1;
pub const LAUNCH_RUNNING: u8 = 2;
pub const LAUNCH_DONE: u8 = 3;

pub const LAUNCH_REST_KPH: f32 = 1.0;
pub const LAUNCH_REST_G: f32 = 0.05;
pub const LAUNCH_ARM_MS: u32 = 1000;
pub const LAUNCH_PUSH_G: f32 = 0.25;
pub const LAUNCH_PUSH_HOLD_MS: u32 = 100;
pub const LAUNCH_ABORT_G: f32 = -0.3;
pub const LAUNCH_TIMEOUT_MS: u32 = 30_000;

pub const LAUNCH_METRIC: LaunchTargets = LaunchTargets {
    sprint_kph: 100.0,
    strip_m: 400.0,
};
pub const LAUNCH_IMPERIAL: LaunchTargets = LaunchTargets {
    sprint_kph: 96.560_64,
    strip_m: 402.336,
};

const KPH_TO_MPS: f32 = 1.0 / 3.6;
const MS_PER_S: f32 = 1000.0;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub struct LaunchTargets {
    pub sprint_kph: f32,
    pub strip_m: f32,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub struct LaunchTimer {
    pub state: u8,
    pub has_still: u8,
    pub has_push: u8,
    pub _pad: u8,
    pub still_since_ms: u32,
    pub push_since_ms: u32,
    pub start_ms: u32,
    pub last_ms: u32,
    pub distance_m: f32,
    pub sprint_ms: u32,
    pub strip_ms: u32,
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub struct LaunchSample {
    pub now_ms: u32,
    pub longitudinal_g: f32,
    pub speed_kph: Option<f32>,
}

pub fn targets_for(imperial: bool) -> LaunchTargets {
    if imperial {
        LAUNCH_IMPERIAL
    } else {
        LAUNCH_METRIC
    }
}

impl LaunchTimer {
    pub fn tick(&mut self, sample: LaunchSample, targets: LaunchTargets) {
        let Some(speed) = sample.speed_kph else {
            self.lose_speed();
            return;
        };
        match self.state {
            LAUNCH_ARMED => self.tick_armed(sample, speed),
            LAUNCH_RUNNING => self.tick_running(sample, speed, targets),
            _ => self.tick_resting(sample, speed),
        }
    }

    pub fn elapsed_ms(&self, now_ms: u32) -> u32 {
        if self.state != LAUNCH_RUNNING {
            return 0;
        }
        now_ms.wrapping_sub(self.start_ms)
    }

    fn lose_speed(&mut self) {
        self.has_still = 0;
        self.has_push = 0;
        if self.state != LAUNCH_DONE {
            self.state = LAUNCH_IDLE;
        }
    }

    fn tick_resting(&mut self, sample: LaunchSample, speed: f32) {
        let at_rest = speed <= LAUNCH_REST_KPH && abs(sample.longitudinal_g) <= LAUNCH_REST_G;
        if !at_rest {
            self.has_still = 0;
            return;
        }
        if self.has_still == 0 {
            self.has_still = 1;
            self.still_since_ms = sample.now_ms;
        }
        if sample.now_ms.wrapping_sub(self.still_since_ms) >= LAUNCH_ARM_MS {
            self.state = LAUNCH_ARMED;
            self.has_push = 0;
        }
    }

    fn tick_armed(&mut self, sample: LaunchSample, speed: f32) {
        if sample.longitudinal_g < LAUNCH_PUSH_G {
            self.has_push = 0;
            if speed > LAUNCH_REST_KPH {
                self.disarm();
            }
            return;
        }
        if self.has_push == 0 {
            self.has_push = 1;
            self.push_since_ms = sample.now_ms;
        }
        if sample.now_ms.wrapping_sub(self.push_since_ms) >= LAUNCH_PUSH_HOLD_MS {
            self.start(sample.now_ms);
        }
    }

    fn disarm(&mut self) {
        self.state = LAUNCH_IDLE;
        self.has_still = 0;
    }

    fn start(&mut self, now_ms: u32) {
        self.state = LAUNCH_RUNNING;
        self.start_ms = self.push_since_ms;
        self.last_ms = now_ms;
        self.distance_m = 0.0;
        self.sprint_ms = 0;
        self.strip_ms = 0;
    }

    fn tick_running(&mut self, sample: LaunchSample, speed: f32, targets: LaunchTargets) {
        let dt_s = sample.now_ms.wrapping_sub(self.last_ms) as f32 / MS_PER_S;
        self.last_ms = sample.now_ms;
        self.distance_m += speed * KPH_TO_MPS * dt_s;
        let elapsed = self.elapsed_ms(sample.now_ms);
        if self.sprint_ms == 0 && speed >= targets.sprint_kph {
            self.sprint_ms = elapsed;
        }
        if self.strip_ms == 0 && self.distance_m >= targets.strip_m {
            self.strip_ms = elapsed;
        }
        if self.finished(sample, elapsed) {
            self.finish();
        }
    }

    fn finished(&self, sample: LaunchSample, elapsed: u32) -> bool {
        let both = self.sprint_ms != 0 && self.strip_ms != 0;
        both || sample.longitudinal_g <= LAUNCH_ABORT_G || elapsed >= LAUNCH_TIMEOUT_MS
    }

    fn finish(&mut self) {
        self.has_still = 0;
        self.state = if self.sprint_ms != 0 || self.strip_ms != 0 {
            LAUNCH_DONE
        } else {
            LAUNCH_IDLE
        };
    }
}

#[cfg(test)]
mod tests;
