use super::*;
use crate::pattern::{TONE_PATTERN_BOOT, TONE_PATTERN_COUNT};

const CRITICAL_CYCLE_MS: u32 = 1_080;

fn freq_at(seq: &mut ToneSequencer, now_ms: u32, critical: bool) -> u16 {
    tick(seq, now_ms, critical).freq_hz
}

#[test]
fn struct_layout_matches_cxx_header() {
    assert_eq!(core::mem::size_of::<ToneSequencer>(), 8);
    assert_eq!(core::mem::size_of::<ToneOutput>(), 4);
}

#[test]
fn every_step_lasts_so_advance_always_terminates() {
    for id in 0..TONE_PATTERN_COUNT {
        assert!(pattern(id).steps.iter().all(|step| step.duration_ms > 0));
    }
}

#[test]
fn idle_is_silent() {
    let mut seq = ToneSequencer::default();
    assert_eq!(tick(&mut seq, 0, false), ToneOutput::default());
    assert_eq!(tick(&mut seq, 10_000, false), ToneOutput::default());
}

#[test]
fn critical_plays_three_beeps_then_rests() {
    let mut seq = ToneSequencer::default();
    let beeps: [(u32, u16); 7] = [
        (1_000, 2_000),
        (1_119, 2_000),
        (1_120, 0),
        (1_200, 2_000),
        (1_400, 2_000),
        (1_520, 0),
        (2_079, 0),
    ];
    for (now_ms, expected) in beeps {
        assert_eq!(freq_at(&mut seq, now_ms, true), expected, "at {now_ms} ms");
    }
}

#[test]
fn critical_repeats_every_cycle() {
    let mut seq = ToneSequencer::default();
    assert_eq!(freq_at(&mut seq, 0, true), 2_000);
    assert_eq!(freq_at(&mut seq, CRITICAL_CYCLE_MS - 1, true), 0);
    assert_eq!(freq_at(&mut seq, CRITICAL_CYCLE_MS, true), 2_000);
    assert_eq!(freq_at(&mut seq, 10 * CRITICAL_CYCLE_MS + 10, true), 2_000);
}

#[test]
fn critical_is_louder_than_boot() {
    let mut seq = ToneSequencer::default();
    let critical = tick(&mut seq, 0, true).amplitude;
    reset(&mut seq);
    assert!(request(&mut seq, TONE_PATTERN_BOOT, 0));
    let boot = tick(&mut seq, 0, false).amplitude;
    assert!(critical > boot && boot > 0);
}

#[test]
fn acknowledging_silences_at_once() {
    let mut seq = ToneSequencer::default();
    assert_eq!(freq_at(&mut seq, 0, true), 2_000);
    assert_eq!(tick(&mut seq, 50, false), ToneOutput::default());
    assert_eq!(seq.pattern, TONE_PATTERN_NONE);
}

#[test]
fn a_new_critical_restarts_on_a_beep() {
    let mut seq = ToneSequencer::default();
    assert_eq!(freq_at(&mut seq, 0, true), 2_000);
    assert_eq!(freq_at(&mut seq, 130, false), 0);
    assert_eq!(freq_at(&mut seq, 130, true), 2_000);
}

#[test]
fn boot_plays_once_then_goes_idle() {
    let mut seq = ToneSequencer::default();
    assert!(request(&mut seq, TONE_PATTERN_BOOT, 500));
    assert_eq!(freq_at(&mut seq, 500, false), 1_320);
    assert_eq!(freq_at(&mut seq, 570, false), 0);
    assert_eq!(freq_at(&mut seq, 600, false), 1_760);
    assert_eq!(freq_at(&mut seq, 710, false), 0);
    assert_eq!(seq.pattern, TONE_PATTERN_NONE);
    assert_eq!(freq_at(&mut seq, 5_000, false), 0);
}

#[test]
fn critical_preempts_boot() {
    let mut seq = ToneSequencer::default();
    assert!(request(&mut seq, TONE_PATTERN_BOOT, 0));
    assert_eq!(freq_at(&mut seq, 10, true), 2_000);
    assert_eq!(seq.pattern, TONE_PATTERN_CRITICAL);
}

#[test]
fn boot_is_refused_while_critical_plays() {
    let mut seq = ToneSequencer::default();
    assert_eq!(freq_at(&mut seq, 0, true), 2_000);
    assert!(!request(&mut seq, TONE_PATTERN_BOOT, 10));
    assert_eq!(seq.pattern, TONE_PATTERN_CRITICAL);
}

#[test]
fn request_takes_one_shot_patterns_only() {
    let mut seq = ToneSequencer::default();
    assert!(!request(&mut seq, TONE_PATTERN_NONE, 0));
    assert!(!request(&mut seq, TONE_PATTERN_CRITICAL, 0));
    assert!(!request(&mut seq, TONE_PATTERN_COUNT, 0));
    assert_eq!(seq, ToneSequencer::default());
}

#[test]
fn survives_the_millis_wrap() {
    let mut seq = ToneSequencer::default();
    let start = u32::MAX - 50;
    assert_eq!(freq_at(&mut seq, start, true), 2_000);
    assert_eq!(freq_at(&mut seq, start.wrapping_add(119), true), 2_000);
    assert_eq!(freq_at(&mut seq, start.wrapping_add(120), true), 0);
    assert_eq!(
        freq_at(&mut seq, start.wrapping_add(CRITICAL_CYCLE_MS), true),
        2_000
    );
}

#[test]
fn unknown_pattern_ids_read_as_silence() {
    let mut seq = ToneSequencer {
        pattern: 200,
        ..ToneSequencer::default()
    };
    assert_eq!(tick(&mut seq, 0, false), ToneOutput::default());
}
