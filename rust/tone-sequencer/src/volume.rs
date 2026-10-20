use crate::pattern::TONE_PATTERN_CRITICAL;

#[cfg(test)]
mod tests;

pub const TONE_VOLUME_MAX_PCT: u8 = 100;
pub const TONE_CRITICAL_VOLUME_FLOOR_PCT: u8 = 50;

pub fn effective_volume(pattern_id: u8, volume_pct: u8) -> u8 {
    let capped = volume_pct.min(TONE_VOLUME_MAX_PCT);
    if pattern_id == TONE_PATTERN_CRITICAL {
        capped.max(TONE_CRITICAL_VOLUME_FLOOR_PCT)
    } else {
        capped
    }
}

pub fn scaled_amplitude(amplitude: i16, pattern_id: u8, volume_pct: u8) -> i16 {
    let pct = i32::from(effective_volume(pattern_id, volume_pct));
    (i32::from(amplitude) * pct / i32::from(TONE_VOLUME_MAX_PCT)) as i16
}
