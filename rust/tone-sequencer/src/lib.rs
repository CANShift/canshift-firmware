#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod pattern;
mod sequencer;
mod synth;

pub use pattern::{
    pattern, Pattern, Step, TONE_PATTERN_BOOT, TONE_PATTERN_COUNT, TONE_PATTERN_CRITICAL,
    TONE_PATTERN_NONE,
};
pub use sequencer::{request, reset, tick, ToneOutput, ToneSequencer};
pub use synth::{fill, ToneSynth, TONE_SAMPLE_RATE_HZ};
