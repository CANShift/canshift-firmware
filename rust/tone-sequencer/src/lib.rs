#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod pattern;
mod sequencer;
mod synth;
mod volume;

pub use pattern::{
    pattern, Pattern, Step, TONE_PATTERN_BOOT, TONE_PATTERN_COUNT, TONE_PATTERN_CRITICAL,
    TONE_PATTERN_NONE, TONE_PATTERN_SHIFT,
};
pub use sequencer::{request, reset, tick, ToneDemand, ToneOutput, ToneSequencer};
pub use synth::{fill, ToneSynth, TONE_SAMPLE_RATE_HZ};
pub use volume::{
    effective_volume, scaled_amplitude, TONE_CRITICAL_VOLUME_FLOOR_PCT, TONE_VOLUME_MAX_PCT,
};
