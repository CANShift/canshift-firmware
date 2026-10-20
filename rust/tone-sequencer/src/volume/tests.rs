use super::*;
use crate::pattern::{TONE_PATTERN_BOOT, TONE_PATTERN_SHIFT};

#[test]
fn critical_floors_at_half_volume() {
    assert_eq!(effective_volume(TONE_PATTERN_CRITICAL, 0), 50);
    assert_eq!(effective_volume(TONE_PATTERN_CRITICAL, 49), 50);
    assert_eq!(effective_volume(TONE_PATTERN_CRITICAL, 80), 80);
}

#[test]
fn other_patterns_follow_the_setting_down_to_silence() {
    assert_eq!(effective_volume(TONE_PATTERN_BOOT, 0), 0);
    assert_eq!(effective_volume(TONE_PATTERN_SHIFT, 30), 30);
}

#[test]
fn volume_caps_at_one_hundred() {
    assert_eq!(effective_volume(TONE_PATTERN_BOOT, 255), 100);
    assert_eq!(scaled_amplitude(12_000, TONE_PATTERN_BOOT, 255), 12_000);
}

#[test]
fn amplitude_scales_linearly() {
    assert_eq!(scaled_amplitude(12_000, TONE_PATTERN_BOOT, 70), 8_400);
    assert_eq!(scaled_amplitude(12_000, TONE_PATTERN_CRITICAL, 10), 6_000);
}
