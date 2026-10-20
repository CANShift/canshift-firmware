use crate::pattern::{pattern, Step, TONE_PATTERN_COUNT, TONE_PATTERN_CRITICAL, TONE_PATTERN_NONE};

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
pub struct ToneOutput {
    pub freq_hz: u16,
    pub amplitude: i16,
}

pub fn reset(seq: &mut ToneSequencer) {
    *seq = ToneSequencer::default();
}

pub fn request(seq: &mut ToneSequencer, id: u8, now_ms: u32) -> bool {
    let playable =
        id != TONE_PATTERN_NONE && id != TONE_PATTERN_CRITICAL && id < TONE_PATTERN_COUNT;
    if !playable || seq.pattern == TONE_PATTERN_CRITICAL {
        return false;
    }
    start(seq, id, now_ms);
    true
}

pub fn tick(seq: &mut ToneSequencer, now_ms: u32, critical: bool) -> ToneOutput {
    follow_critical(seq, critical, now_ms);
    advance(seq, now_ms);
    output(seq)
}

fn start(seq: &mut ToneSequencer, id: u8, now_ms: u32) {
    seq.pattern = id;
    seq.step = 0;
    seq.step_started_ms = now_ms;
}

fn follow_critical(seq: &mut ToneSequencer, critical: bool, now_ms: u32) {
    let playing = seq.pattern == TONE_PATTERN_CRITICAL;
    if critical == playing {
        return;
    }
    if critical {
        start(seq, TONE_PATTERN_CRITICAL, now_ms);
    } else {
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

fn output(seq: &ToneSequencer) -> ToneOutput {
    match current_step(seq) {
        Some(step) if step.freq_hz != 0 => ToneOutput {
            freq_hz: step.freq_hz,
            amplitude: pattern(seq.pattern).amplitude,
        },
        _ => ToneOutput::default(),
    }
}
