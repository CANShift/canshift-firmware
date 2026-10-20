use super::*;

const LOUD: ToneOutput = ToneOutput {
    freq_hz: 2_205,
    amplitude: 1_000,
};
const SAMPLES_PER_PERIOD_AT_2205_HZ: usize = 20;

#[test]
fn struct_layout_matches_cxx_header() {
    assert_eq!(core::mem::size_of::<ToneSynth>(), 4);
}

#[test]
fn silence_writes_zeros_and_rewinds_the_phase() {
    let mut synth = ToneSynth { phase: 12_345 };
    let mut out = [7i16; 8];
    fill(&mut synth, &mut out, ToneOutput::default());
    assert_eq!(out, [0; 8]);
    assert_eq!(synth.phase, 0);
}

#[test]
fn non_positive_amplitude_is_silence() {
    let mut synth = ToneSynth::default();
    let mut out = [7i16; 4];
    let tone = ToneOutput {
        freq_hz: 2_000,
        amplitude: -5,
    };
    fill(&mut synth, &mut out, tone);
    assert_eq!(out, [0; 4]);
}

#[test]
fn square_wave_spends_half_a_period_high() {
    let mut synth = ToneSynth::default();
    let mut out = [0i16; SAMPLES_PER_PERIOD_AT_2205_HZ * 2];
    fill(&mut synth, &mut out, LOUD);
    let (high, low) = out.split_at(SAMPLES_PER_PERIOD_AT_2205_HZ);
    assert!(high.iter().all(|&s| s == 1_000));
    assert!(low.iter().all(|&s| s == -1_000));
}

#[test]
fn both_channels_carry_the_same_sample() {
    let mut synth = ToneSynth::default();
    let mut out = [0i16; 64];
    fill(&mut synth, &mut out, LOUD);
    assert!(out.chunks_exact(2).all(|frame| frame[0] == frame[1]));
}

#[test]
fn phase_carries_across_buffers() {
    let mut whole_synth = ToneSynth::default();
    let mut whole = [0i16; 40];
    fill(&mut whole_synth, &mut whole, LOUD);
    let mut split_synth = ToneSynth::default();
    let mut split = [0i16; 40];
    let (first, second) = split.split_at_mut(14);
    fill(&mut split_synth, first, LOUD);
    fill(&mut split_synth, second, LOUD);
    assert_eq!(whole, split);
    assert_eq!(whole_synth, split_synth);
}

#[test]
fn an_odd_trailing_sample_is_zeroed() {
    let mut synth = ToneSynth::default();
    let mut out = [9i16; 5];
    fill(&mut synth, &mut out, LOUD);
    assert_eq!(out[4], 0);
}

#[test]
fn frequencies_clamp_at_nyquist() {
    let mut synth = ToneSynth::default();
    let mut out = [0i16; 8];
    let tone = ToneOutput {
        freq_hz: u16::MAX,
        amplitude: 1_000,
    };
    fill(&mut synth, &mut out, tone);
    assert_eq!(
        out,
        [1_000, 1_000, -1_000, -1_000, 1_000, 1_000, -1_000, -1_000]
    );
}
