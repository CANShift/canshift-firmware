#![allow(clippy::missing_safety_doc)]

use crate::sequencer::{request, reset, tick, ToneOutput, ToneSequencer};
use crate::synth::{fill, ToneSynth};

const _: () = assert!(core::mem::size_of::<ToneSequencer>() == 8);
const _: () = assert!(core::mem::size_of::<ToneOutput>() == 4);
const _: () = assert!(core::mem::size_of::<ToneSynth>() == 4);

#[no_mangle]
pub unsafe extern "C" fn tone_sequencer_reset_rs(seq: *mut ToneSequencer) {
    if let Some(seq) = unsafe { seq.as_mut() } {
        reset(seq);
    }
}

#[no_mangle]
pub unsafe extern "C" fn tone_sequencer_request_rs(
    seq: *mut ToneSequencer,
    pattern: u8,
    now_ms: u32,
) -> bool {
    let Some(seq) = (unsafe { seq.as_mut() }) else {
        return false;
    };
    request(seq, pattern, now_ms)
}

#[no_mangle]
pub unsafe extern "C" fn tone_sequencer_tick_rs(
    seq: *mut ToneSequencer,
    now_ms: u32,
    critical: bool,
) -> ToneOutput {
    let Some(seq) = (unsafe { seq.as_mut() }) else {
        return ToneOutput::default();
    };
    tick(seq, now_ms, critical)
}

#[no_mangle]
pub unsafe extern "C" fn tone_synth_fill_rs(
    synth: *mut ToneSynth,
    out: *mut i16,
    samples: usize,
    tone: ToneOutput,
) {
    let Some(synth) = (unsafe { synth.as_mut() }) else {
        return;
    };
    if out.is_null() {
        return;
    }
    let out = unsafe { core::slice::from_raw_parts_mut(out, samples) };
    fill(synth, out, tone);
}
