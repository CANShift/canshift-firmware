use crate::sequencer::ToneOutput;

#[cfg(test)]
mod tests;

pub const TONE_SAMPLE_RATE_HZ: u32 = 44100;

const STEREO_CHANNELS: usize = 2;
const PHASE_HALF: u32 = 1 << 31;
const NYQUIST_HZ: u16 = (TONE_SAMPLE_RATE_HZ / 2) as u16;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct ToneSynth {
    pub phase: u32,
}

pub fn fill(synth: &mut ToneSynth, out: &mut [i16], tone: ToneOutput) {
    if tone.freq_hz == 0 || tone.amplitude <= 0 {
        synth.phase = 0;
        out.fill(0);
        return;
    }
    let step = phase_step(tone.freq_hz);
    let (frames, tail) = out.as_chunks_mut::<STEREO_CHANNELS>();
    for frame in frames {
        *frame = [square(synth.phase, tone.amplitude); STEREO_CHANNELS];
        synth.phase = synth.phase.wrapping_add(step);
    }
    tail.fill(0);
}

fn square(phase: u32, amplitude: i16) -> i16 {
    if phase < PHASE_HALF {
        amplitude
    } else {
        -amplitude
    }
}

fn phase_step(freq_hz: u16) -> u32 {
    let clamped = u64::from(freq_hz.min(NYQUIST_HZ));
    let rate = u64::from(TONE_SAMPLE_RATE_HZ);
    (((clamped << 32) + rate / 2) / rate) as u32
}
