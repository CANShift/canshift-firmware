pub const TONE_PATTERN_NONE: u8 = 0;
pub const TONE_PATTERN_CRITICAL: u8 = 1;
pub const TONE_PATTERN_BOOT: u8 = 2;
pub const TONE_PATTERN_SHIFT: u8 = 3;
pub const TONE_PATTERN_COUNT: u8 = 4;

const SILENT_HZ: u16 = 0;
const CRITICAL_HZ: u16 = 2_000;
const CRITICAL_BEEP_MS: u16 = 120;
const CRITICAL_GAP_MS: u16 = 80;
const CRITICAL_REST_MS: u16 = 560;
const CRITICAL_AMPLITUDE: i16 = 12_000;
const BOOT_LOW_HZ: u16 = 1_320;
const BOOT_HIGH_HZ: u16 = 1_760;
const BOOT_LOW_MS: u16 = 70;
const BOOT_GAP_MS: u16 = 30;
const BOOT_HIGH_MS: u16 = 110;
const BOOT_AMPLITUDE: i16 = 4_000;
const SHIFT_HZ: u16 = 2_600;
const SHIFT_BEEP_MS: u16 = 60;
const SHIFT_REST_MS: u16 = 107;
const SHIFT_AMPLITUDE: i16 = 6_000;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Step {
    pub freq_hz: u16,
    pub duration_ms: u16,
}

pub struct Pattern {
    pub steps: &'static [Step],
    pub repeats: bool,
    pub amplitude: i16,
}

const fn beep(freq_hz: u16, duration_ms: u16) -> Step {
    Step {
        freq_hz,
        duration_ms,
    }
}

const fn rest(duration_ms: u16) -> Step {
    beep(SILENT_HZ, duration_ms)
}

static CRITICAL_STEPS: [Step; 6] = [
    beep(CRITICAL_HZ, CRITICAL_BEEP_MS),
    rest(CRITICAL_GAP_MS),
    beep(CRITICAL_HZ, CRITICAL_BEEP_MS),
    rest(CRITICAL_GAP_MS),
    beep(CRITICAL_HZ, CRITICAL_BEEP_MS),
    rest(CRITICAL_REST_MS),
];

static BOOT_STEPS: [Step; 3] = [
    beep(BOOT_LOW_HZ, BOOT_LOW_MS),
    rest(BOOT_GAP_MS),
    beep(BOOT_HIGH_HZ, BOOT_HIGH_MS),
];

static SHIFT_STEPS: [Step; 2] = [beep(SHIFT_HZ, SHIFT_BEEP_MS), rest(SHIFT_REST_MS)];

static PATTERNS: [Pattern; TONE_PATTERN_COUNT as usize] = [
    Pattern {
        steps: &[],
        repeats: false,
        amplitude: 0,
    },
    Pattern {
        steps: &CRITICAL_STEPS,
        repeats: true,
        amplitude: CRITICAL_AMPLITUDE,
    },
    Pattern {
        steps: &BOOT_STEPS,
        repeats: false,
        amplitude: BOOT_AMPLITUDE,
    },
    Pattern {
        steps: &SHIFT_STEPS,
        repeats: true,
        amplitude: SHIFT_AMPLITUDE,
    },
];

pub fn pattern(id: u8) -> &'static Pattern {
    PATTERNS
        .get(usize::from(id))
        .unwrap_or(&PATTERNS[usize::from(TONE_PATTERN_NONE)])
}
