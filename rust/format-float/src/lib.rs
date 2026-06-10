// Rust port of FloatFormat::{formatFixed, formatFromSpec, formatGeneral} (#1177 R-2).
// Avoids newlib %f/%g — that path drags ~30 KB flash (#305 / #405).
// snprintf-style returns: chars-that-would-be-written; truncate-not-round; NaN/inf as literals.
#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

// Required for no_std staticlib — reaching here means invariant break.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

const MAX_DECIMALS: i32 = 9;
const MAX_SIG_DIGITS: i32 = 9;

const POW10: [i64; 10] = [
    1, 10, 100, 1_000, 10_000, 100_000, 1_000_000, 10_000_000, 100_000_000, 1_000_000_000,
];

#[must_use]
pub fn format_fixed(buf: &mut [u8], value: f32, decimals: i32) -> usize {
    if buf.is_empty() {
        return 0;
    }
    let decimals = decimals.clamp(0, MAX_DECIMALS);

    if value.is_nan() {
        return copy_terminated(buf, b"nan");
    }
    if value.is_infinite() {
        return copy_terminated(buf, if value < 0.0 { b"-inf" } else { b"inf" });
    }

    let negative = value < 0.0;
    let abs_value = if negative { -value } else { value };

    let scale = POW10[decimals as usize];
    let scaled = (abs_value * (scale as f32) + 0.5) as i64;

    let whole = scaled / scale;
    let frac = scaled % scale;

    // Worst case 23 bytes; 32 matches the C++ scratch.
    let mut scratch = [0u8; 32];
    let mut pos = 0usize;
    if negative {
        scratch[pos] = b'-';
        pos += 1;
    }
    pos += write_unsigned_int(&mut scratch[pos..], whole);
    if decimals > 0 {
        scratch[pos] = b'.';
        pos += 1;
        pos += write_fixed_width_int(&mut scratch[pos..], frac, decimals as usize);
    }

    copy_terminated(buf, &scratch[..pos])
}

// Parses "<prefix>%[.N]f<suffix>" from JSON configs. Falls back to %.1f on bad spec.
#[must_use]
pub fn format_from_spec(buf: &mut [u8], value: f32, spec: &[u8]) -> usize {
    if buf.is_empty() {
        return 0;
    }
    if spec.is_empty() {
        return format_fixed(buf, value, 1);
    }

    // Scan for the first `%[.N]f` token. `%%` is the escape — skip both.
    let mut percent_off: Option<usize> = None;
    let mut decimals = 1i32;
    let mut after_spec_off = 0usize;
    let mut i = 0usize;
    while i < spec.len() {
        if spec[i] == b'%' && i + 1 < spec.len() && spec[i + 1] == b'%' {
            i += 2;
            continue;
        }
        if spec[i] == b'%' {
            let mut j = i + 1;
            let mut dec: i32 = 1;
            if j < spec.len() && spec[j] == b'.' {
                j += 1;
                let mut n: i32 = 0;
                let mut parsed = 0;
                let mut bail = false;
                while j < spec.len() && spec[j].is_ascii_digit() {
                    n = n * 10 + ((spec[j] - b'0') as i32);
                    j += 1;
                    parsed += 1;
                    if parsed > 2 {
                        bail = true;
                        break;
                    }
                }
                if bail {
                    i = j;
                    continue;
                }
                dec = n;
            }
            if j < spec.len() && spec[j] == b'f' {
                percent_off = Some(i);
                decimals = dec;
                after_spec_off = j + 1;
                break;
            }
            i = j + 1;
            continue;
        }
        i += 1;
    }

    let Some(percent) = percent_off else {
        return format_fixed(buf, value, 1);
    };

    let mut number_scratch = [0u8; 32];
    let number_len_total = format_fixed(&mut number_scratch, value, decimals);
    let number = &number_scratch[..number_len_total];

    let prefix = &spec[..percent];
    let suffix = &spec[after_spec_off..];
    let total = prefix.len() + number.len() + suffix.len();

    let mut out = 0usize;
    append_bounded(buf, &mut out, prefix);
    append_bounded(buf, &mut out, number);
    append_bounded(buf, &mut out, suffix);
    if out < buf.len() {
        buf[out] = 0;
    } else {
        buf[buf.len() - 1] = 0;
    }
    total
}

// printf %g equivalent — fixed notation, trailing zeros dropped. Output is
// valid JSON numeric (Studio parses telemetry as JSON).
#[must_use]
pub fn format_general(buf: &mut [u8], value: f32, sig_digits: i32) -> usize {
    if buf.is_empty() {
        return 0;
    }
    let sig_digits = sig_digits.clamp(1, MAX_SIG_DIGITS);

    if value.is_nan() {
        return copy_terminated(buf, b"nan");
    }
    if value.is_infinite() {
        return copy_terminated(buf, if value < 0.0 { b"-inf" } else { b"inf" });
    }

    // 12-iteration cap guards +inf-adjacent values; mirrors C++ exactly.
    let abs = value.abs();
    let mut int_digits = 1;
    if abs >= 1.0 {
        let mut scan = abs;
        int_digits = 0;
        while scan >= 1.0 && int_digits < 12 {
            scan /= 10.0;
            int_digits += 1;
        }
    }
    let mut decimals = sig_digits - int_digits;
    if decimals < 0 {
        decimals = 0;
    }
    if decimals > MAX_DECIMALS {
        decimals = MAX_DECIMALS;
    }

    let mut scratch = [0u8; 32];
    let len = format_fixed(&mut scratch, value, decimals);
    // If the scratch overflowed (>= 31 chars rendered + NUL), copy what's
    // in scratch as-is (it's the same as C++ behaviour — `copyTerminated`
    // of the scratch).
    if len >= scratch.len() - 1 {
        return copy_terminated(buf, &scratch[..find_nul(&scratch)]);
    }

    // Strip trailing zeros after the decimal point (the %g trick).
    let mut end = len;
    if decimals > 0 {
        while end > 0 && scratch[end - 1] == b'0' {
            end -= 1;
        }
        if end > 0 && scratch[end - 1] == b'.' {
            end -= 1;
        }
    }

    copy_terminated(buf, &scratch[..end])
}

#[inline]
fn find_nul(buf: &[u8]) -> usize {
    buf.iter().position(|&b| b == 0).unwrap_or(buf.len())
}

// snprintf-style — returns src.len(), not copied length.
fn copy_terminated(buf: &mut [u8], src: &[u8]) -> usize {
    if buf.is_empty() {
        return 0;
    }
    let copy_len = src.len().min(buf.len() - 1);
    buf[..copy_len].copy_from_slice(&src[..copy_len]);
    buf[copy_len] = 0;
    src.len()
}

fn append_bounded(buf: &mut [u8], out: &mut usize, src: &[u8]) {
    if *out >= buf.len().saturating_sub(1) {
        return;
    }
    let room = buf.len() - 1 - *out;
    let copy = src.len().min(room);
    buf[*out..*out + copy].copy_from_slice(&src[..copy]);
    *out += copy;
}

fn write_fixed_width_int(buf: &mut [u8], mut value: i64, digits: usize) -> usize {
    for i in (0..digits).rev() {
        buf[i] = b'0' + ((value % 10) as u8);
        value /= 10;
    }
    digits
}

fn write_unsigned_int(buf: &mut [u8], value: i64) -> usize {
    if value == 0 {
        buf[0] = b'0';
        return 1;
    }
    let mut tmp = [0u8; 24];
    let mut n = 0usize;
    let mut v = value;
    while v > 0 && n < tmp.len() {
        tmp[n] = b'0' + ((v % 10) as u8);
        v /= 10;
        n += 1;
    }
    for i in 0..n {
        buf[i] = tmp[n - 1 - i];
    }
    n
}

#[cfg(test)]
mod tests {
    use super::*;

    fn as_str(buf: &[u8]) -> &str {
        let end = find_nul(buf);
        core::str::from_utf8(&buf[..end]).unwrap()
    }

    #[test]
    fn fixed_basic_two_decimals() {
        let mut buf = [0u8; 32];
        let n = format_fixed(&mut buf, 3.14159, 2);
        assert_eq!(n, 4);
        assert_eq!(as_str(&buf), "3.14");
    }

    #[test]
    fn fixed_zero_decimals_rounds() {
        let mut buf = [0u8; 32];
        let n = format_fixed(&mut buf, 3.7, 0);
        assert_eq!(n, 1);
        assert_eq!(as_str(&buf), "4");
    }

    #[test]
    fn fixed_negative_value() {
        let mut buf = [0u8; 32];
        format_fixed(&mut buf, -12.5, 1);
        assert_eq!(as_str(&buf), "-12.5");
    }

    #[test]
    fn fixed_handles_zero() {
        let mut buf = [0u8; 32];
        format_fixed(&mut buf, 0.0, 3);
        assert_eq!(as_str(&buf), "0.000");
    }

    #[test]
    fn fixed_nan() {
        let mut buf = [0u8; 32];
        let n = format_fixed(&mut buf, f32::NAN, 2);
        assert_eq!(n, 3);
        assert_eq!(as_str(&buf), "nan");
    }

    #[test]
    fn fixed_positive_infinity() {
        let mut buf = [0u8; 32];
        format_fixed(&mut buf, f32::INFINITY, 2);
        assert_eq!(as_str(&buf), "inf");
    }

    #[test]
    fn fixed_negative_infinity() {
        let mut buf = [0u8; 32];
        format_fixed(&mut buf, f32::NEG_INFINITY, 2);
        assert_eq!(as_str(&buf), "-inf");
    }

    #[test]
    fn fixed_clamps_decimals_high() {
        let mut buf = [0u8; 32];
        format_fixed(&mut buf, 1.0, 99);
        assert_eq!(as_str(&buf), "1.000000000");
    }

    #[test]
    fn fixed_clamps_decimals_negative() {
        let mut buf = [0u8; 32];
        format_fixed(&mut buf, 1.5, -1);
        assert_eq!(as_str(&buf), "2");
    }

    #[test]
    fn fixed_truncates_to_buffer_with_nul() {
        let mut buf = [0u8; 4];
        let n = format_fixed(&mut buf, 123.456, 2);
        assert_eq!(n, 6);
        assert_eq!(as_str(&buf), "123");
        assert_eq!(buf[3], 0);
    }

    #[test]
    fn fixed_empty_buffer_returns_zero() {
        let mut buf = [];
        assert_eq!(format_fixed(&mut buf, 1.0, 2), 0);
    }

    #[test]
    fn spec_no_token_falls_back_to_one_decimal() {
        let mut buf = [0u8; 32];
        let n = format_from_spec(&mut buf, 12.34, b" V");
        assert_eq!(n, 4);
        assert_eq!(as_str(&buf), "12.3");
    }

    #[test]
    fn spec_default_precision() {
        let mut buf = [0u8; 32];
        format_from_spec(&mut buf, 5.4321, b"%fV");
        assert_eq!(as_str(&buf), "5.4V");
    }

    #[test]
    fn spec_explicit_precision() {
        let mut buf = [0u8; 32];
        format_from_spec(&mut buf, 5.4321, b"%.3fV");
        assert_eq!(as_str(&buf), "5.432V");
    }

    #[test]
    fn spec_zero_precision() {
        let mut buf = [0u8; 32];
        format_from_spec(&mut buf, 5.7, b"%.0f bar");
        assert_eq!(as_str(&buf), "6 bar");
    }

    #[test]
    fn spec_prefix_and_suffix() {
        let mut buf = [0u8; 32];
        format_from_spec(&mut buf, 3.14, b"value=%.2fkg!");
        assert_eq!(as_str(&buf), "value=3.14kg!");
    }

    #[test]
    fn spec_double_percent_is_skip_not_unescape() {
        let mut buf = [0u8; 32];
        // C++ quirk: %% survives in the prefix verbatim, NOT printf-style.
        format_from_spec(&mut buf, 9.8, b"%%V%.1f");
        assert_eq!(as_str(&buf), "%%V9.8");
    }

    #[test]
    fn spec_unreasonable_precision_bails_and_keeps_scanning() {
        let mut buf = [0u8; 32];
        format_from_spec(&mut buf, 4.5, b"%.123fV");
        assert_eq!(as_str(&buf), "4.5");
    }

    #[test]
    fn spec_empty_string_is_one_decimal() {
        let mut buf = [0u8; 32];
        format_from_spec(&mut buf, 1.2345, b"");
        assert_eq!(as_str(&buf), "1.2");
    }

    #[test]
    fn general_strips_trailing_zeros() {
        let mut buf = [0u8; 32];
        format_general(&mut buf, 1.5, 4);
        // 4 sig digits, 1 int digit → 3 decimals → "1.500" → strip → "1.5"
        assert_eq!(as_str(&buf), "1.5");
    }

    #[test]
    fn general_keeps_meaningful_decimals() {
        let mut buf = [0u8; 32];
        format_general(&mut buf, 1.234, 4);
        assert_eq!(as_str(&buf), "1.234");
    }

    #[test]
    fn general_large_magnitude_no_decimals() {
        let mut buf = [0u8; 32];
        format_general(&mut buf, 12345.0, 4);
        // 4 sig digits, 5 int digits → 0 decimals → "12345" (no trailing
        // strip needed).
        assert_eq!(as_str(&buf), "12345");
    }

    #[test]
    fn general_sub_unit_small() {
        let mut buf = [0u8; 32];
        // abs < 1 → int_digits stays at 1 (the early return path), so
        // 4 sig digits → 3 decimals → "0.123" with the trailing strip.
        format_general(&mut buf, 0.123, 4);
        assert_eq!(as_str(&buf), "0.123");
    }

    #[test]
    fn general_negative() {
        let mut buf = [0u8; 32];
        format_general(&mut buf, -1.5, 3);
        assert_eq!(as_str(&buf), "-1.5");
    }

    #[test]
    fn general_clamps_sig_digits_low() {
        let mut buf = [0u8; 32];
        format_general(&mut buf, 9.876, 0);
        // Clamped to 1 → 1 int digit → 0 decimals → "10" after rounding.
        assert_eq!(as_str(&buf), "10");
    }

    #[test]
    fn general_nan_and_inf() {
        let mut buf = [0u8; 32];
        format_general(&mut buf, f32::NAN, 3);
        assert_eq!(as_str(&buf), "nan");

        format_general(&mut buf, f32::INFINITY, 3);
        assert_eq!(as_str(&buf), "inf");

        format_general(&mut buf, f32::NEG_INFINITY, 3);
        assert_eq!(as_str(&buf), "-inf");
    }
}
