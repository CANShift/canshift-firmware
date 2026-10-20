use crate::pattern::{
    pattern, Step, TONE_PATTERN_COUNT, TONE_PATTERN_CRITICAL, TONE_PATTERN_NONE, TONE_PATTERN_SHIFT,
};
use crate::volume::scaled_amplitude;

#[cfg(test)]
mod tests;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct ToneSequencer {
    pub step_started_ms: u32,
    pub pattern: u8,
    pub step: u8,
    pub _pad: [u8; 2],
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct ToneDemand {
    pub volume_pct: u8,
    pub critical: bool,
    pub shift: bool,
    pub _pad: u8,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct ToneOutput {
    pub freq_hz: u16,
    pub amplitude: i16,
}

pub fn reset(seq: &mut ToneSequencer) {
    *seq = ToneSequencer::default();
}

pub fn request(seq: &mut ToneSequencer, id: u8, now_ms: u32) -> bool {
    let one_shot = id != TONE_PATTERN_NONE && id < TONE_PATTERN_COUNT && !pattern(id).repeats;
    if !one_shot || pattern(seq.pattern).repeats {
        return false;
    }
    start(seq, id, now_ms);
    true
}

pub fn tick(seq: &mut ToneSequencer, now_ms: u32, demand: ToneDemand) -> ToneOutput {
    follow_held(seq, held_pattern(demand), now_ms);
    advance(seq, now_ms);
    output(seq, demand.volume_pct)
}

fn start(seq: &mut ToneSequencer, id: u8, now_ms: u32) {
    seq.pattern = id;
    seq.step = 0;
    seq.step_started_ms = now_ms;
}

fn held_pattern(demand: ToneDemand) -> u8 {
    [
        (demand.critical, TONE_PATTERN_CRITICAL),
        (demand.shift, TONE_PATTERN_SHIFT),
    ]
    .into_iter()
    .find(|&(wanted, _)| wanted)
    .map_or(TONE_PATTERN_NONE, |(_, id)| id)
}

fn follow_held(seq: &mut ToneSequencer, wanted: u8, now_ms: u32) {
    if seq.pattern == wanted {
        return;
    }
    if wanted != TONE_PATTERN_NONE {
        start(seq, wanted, now_ms);
    } else if pattern(seq.pattern).repeats {
        reset(seq);
    }
}

fn current_step(seq: &ToneSequencer) -> Option<&'static Step> {
    pattern(seq.pattern).steps.get(usize::from(seq.step))
}

fn advance(seq: &mut ToneSequencer, now_ms: u32) {
    while let Some(step) = current_step(seq) {
        let duration_ms = u32::from(step.duration_ms);
        if now_ms.wrapping_sub(seq.step_started_ms) < duration_ms {
            return;
        }
        seq.step_started_ms = seq.step_started_ms.wrapping_add(duration_ms);
        seq.step += 1;
        wrap_or_finish(seq);
    }
}

fn wrap_or_finish(seq: &mut ToneSequencer) {
    let current = pattern(seq.pattern);
    if usize::from(seq.step) < current.steps.len() {
        return;
    }
    if current.repeats {
        seq.step = 0;
    } else {
        reset(seq);
    }
}

fn output(seq: &ToneSequencer, volume_pct: u8) -> ToneOutput {
    match current_step(seq) {
        Some(step) if step.freq_hz != 0 => ToneOutput {
            freq_hz: step.freq_hz,
            amplitude: scaled_amplitude(pattern(seq.pattern).amplitude, seq.pattern, volume_pct),
        },
        _ => ToneOutput::default(),
    }
}
