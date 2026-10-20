#![cfg_attr(not(feature = "std"), no_std)]

pub mod cuts;

#[cfg(feature = "ffi")]
mod critical_ack_ffi;
#[cfg(feature = "ffi")]
mod cuts_ffi;
#[cfg(feature = "ffi")]
mod ffi;

pub mod bus_silence;

pub mod critical_alert;

pub mod critical_ack;

pub mod display_power;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug, Default)]
pub enum AlertLevel {
    #[default]
    Normal = 0,
    Caution = 1,
    Warning = 2,
    Critical = 3,
}

const _: () = assert!(core::mem::size_of::<AlertLevel>() == 1);

impl AlertLevel {
    #[inline]
    #[must_use]
    pub fn max(self, other: Self) -> Self {
        if (self as u8) > (other as u8) {
            self
        } else {
            other
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct SensorHealth {
    pub ever_valid: bool,
    pub lost: bool,
    pub prev_valid: bool,
    pub valid_since_ms: u32,
}

#[must_use]
pub fn sensor_health_step(
    mut h: SensorHealth,
    valid: bool,
    now_ms: u32,
    clear_hold_ms: u32,
) -> SensorHealth {
    if valid {
        if !h.prev_valid {
            h.valid_since_ms = now_ms;
        }
        h.ever_valid = true;
        if h.lost && now_ms.wrapping_sub(h.valid_since_ms) >= clear_hold_ms {
            h.lost = false;
        }
    } else if h.ever_valid {
        h.lost = true;
    }
    h.prev_valid = valid;
    h
}

#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Debug)]
pub struct LevelHold {
    pub level: AlertLevel,
    pub raised_at_ms: u32,
}

#[must_use]
pub fn level_hold_step(
    mut h: LevelHold,
    raw: AlertLevel,
    sticky: AlertLevel,
    now_ms: u32,
    min_active_ms: u32,
) -> LevelHold {
    let sticky = sticky.max(raw);
    if raw > h.level {
        h.level = raw;
        h.raised_at_ms = now_ms;
        return h;
    }
    if sticky >= h.level || now_ms.wrapping_sub(h.raised_at_ms) < min_active_ms {
        return h;
    }
    h.level = sticky;
    h.raised_at_ms = now_ms;
    h
}

#[must_use]
pub fn eval_with_hysteresis<F: Fn(f32) -> AlertLevel>(
    h: LevelHold,
    value: f32,
    now_ms: u32,
    hysteresis_pct: f32,
    min_active_ms: u32,
    eval: F,
) -> LevelHold {
    let band = value.abs() * (hysteresis_pct / 100.0);
    let raw = eval(value);
    let sticky = eval(value + band).max(eval(value - band));
    level_hold_step(h, raw, sticky, now_ms, min_active_ms)
}

#[must_use]
pub fn eval_high_side(value: f32, high_warn: f32, high_crit: f32) -> AlertLevel {
    if !high_crit.is_nan() && value > high_crit {
        return AlertLevel::Critical;
    }
    if !high_warn.is_nan() && value > high_warn {
        return AlertLevel::Warning;
    }
    AlertLevel::Normal
}

#[must_use]
pub fn eval_rev_limiter(rpm: f32, rev_limit_rpm: f32, warn_pct: u8, flash_pct: u8) -> AlertLevel {
    let warn_rpm = rev_limit_rpm * (warn_pct as f32 / 100.0);
    let flash_rpm = rev_limit_rpm * (flash_pct as f32 / 100.0);
    if rpm >= flash_rpm {
        return AlertLevel::Critical;
    }
    if rpm >= warn_rpm {
        return AlertLevel::Warning;
    }
    AlertLevel::Normal
}

#[must_use]
pub fn rev_limit_row_lit(elapsed_ms: u32, blink_hz: u8) -> bool {
    if blink_hz == 0 {
        return true;
    }
    let half_periods = (elapsed_ms as u64 * 2 * blink_hz as u64) / 1000;
    (half_periods & 1) == 0
}

#[must_use]
pub fn eval_coolant_temp(
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
) -> AlertLevel {
    let caution = if temp_c >= warn_c - 5.0 {
        AlertLevel::Caution
    } else {
        AlertLevel::Normal
    };
    eval_high_side_temp(temp_c, warn_c, crit_c, high_warn_c, high_crit_c).max(caution)
}

#[must_use]
pub fn eval_high_side_temp(
    temp_c: f32,
    warn_c: f32,
    crit_c: f32,
    high_warn_c: f32,
    high_crit_c: f32,
) -> AlertLevel {
    let base = if temp_c >= crit_c {
        AlertLevel::Critical
    } else if temp_c >= warn_c {
        AlertLevel::Warning
    } else {
        AlertLevel::Normal
    };
    base.max(eval_high_side(temp_c, high_warn_c, high_crit_c))
}

#[must_use]
pub fn eval_oil_pressure(
    press_bar: f32,
    warn_bar: f32,
    crit_bar: f32,
    high_warn_bar: f32,
    high_crit_bar: f32,
) -> AlertLevel {
    let base = if press_bar <= crit_bar {
        AlertLevel::Critical
    } else if press_bar <= warn_bar {
        AlertLevel::Warning
    } else {
        AlertLevel::Normal
    };
    base.max(eval_high_side(press_bar, high_warn_bar, high_crit_bar))
}

#[must_use]
pub fn eval_battery(
    volts: f32,
    low_warn_v: f32,
    low_crit_v: f32,
    high_warn_v: f32,
    high_crit_v: f32,
) -> AlertLevel {
    if volts < low_crit_v {
        return AlertLevel::Critical;
    }
    let base = if volts < low_warn_v {
        AlertLevel::Warning
    } else {
        AlertLevel::Normal
    };
    base.max(eval_high_side(volts, high_warn_v, high_crit_v))
}

pub const SEVERITY_LEVEL_COUNT: u8 = 4;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug, Default)]
pub enum Severity {
    #[default]
    Information = 0,
    Warning = 1,
    Critical = 2,
    Failure = 3,
}

#[must_use]
pub(crate) fn crossed(value: f32, threshold: f32, below: bool) -> bool {
    if threshold.is_nan() {
        return false;
    }
    if below {
        value <= threshold
    } else {
        value >= threshold
    }
}

pub const DISPLAY_IDLE_LIVE: u8 = 0;
pub const DISPLAY_IDLE_DIM: u8 = 1;
pub const DISPLAY_IDLE_OFF: u8 = 2;

#[must_use]
pub fn display_idle_state(idle_ms: u32, dim_after_ms: u32, off_after_ms: u32) -> u8 {
    if off_after_ms > 0 && idle_ms >= off_after_ms {
        return DISPLAY_IDLE_OFF;
    }
    if dim_after_ms > 0 && idle_ms >= dim_after_ms {
        return DISPLAY_IDLE_DIM;
    }
    DISPLAY_IDLE_LIVE
}

#[must_use]
pub fn severity_for_reading(
    value: f32,
    warn_level: f32,
    danger_level: f32,
    danger_below: bool,
) -> Severity {
    if value.is_nan() {
        return Severity::Information;
    }
    if crossed(value, danger_level, danger_below) {
        return Severity::Failure;
    }
    if crossed(value, warn_level, danger_below) {
        return Severity::Warning;
    }
    Severity::Information
}

#[must_use]
pub fn severity_for_signal(
    value: f32,
    warn_level: f32,
    danger_level: f32,
    high_warn_level: f32,
    high_danger_level: f32,
) -> Severity {
    let primary_below = warn_level > danger_level;
    let primary = severity_for_reading(value, warn_level, danger_level, primary_below);
    let high = severity_for_reading(value, high_warn_level, high_danger_level, false);
    primary.max(high)
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub struct CrossedLimit {
    pub limit: f32,
    pub below: bool,
    pub valid: bool,
}

#[must_use]
pub fn crossed_limit(
    value: f32,
    primary: f32,
    primary_below: bool,
    high_crit: f32,
) -> CrossedLimit {
    if crossed(value, high_crit, false) {
        return CrossedLimit {
            limit: high_crit,
            below: false,
            valid: true,
        };
    }
    if crossed(value, primary, primary_below) {
        return CrossedLimit {
            limit: primary,
            below: primary_below,
            valid: true,
        };
    }
    CrossedLimit::default()
}

#[cfg(test)]
mod tests;
