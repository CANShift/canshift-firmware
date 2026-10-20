#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

const MAX_DECIMALS: i32 = 9;
const MAX_SIG_DIGITS: i32 = 9;

const POW10: [i64; 10] = [
    1,
    10,
    100,
    1_000,
    10_000,
    100_000,
    1_000_000,
    10_000_000,
    100_000_000,
    1_000_000_000,
];

#[must_use]
pub fn format_fixed(buf: &mut [u8], value: f32, decimals: i32) -> usize {
    if buf.is_empty() {
        return 0;
    }
    let decimals = decimals.clamp(0, MAX_DECIMALS);
    if let Some(len) = copy_non_finite(buf, value) {
        return len;
    }
    let negative = value < 0.0;
    let abs_value = if negative {
        -value
    } else {
        value
    };

    let scale = POW10[decimals as usize];
    let scaled = (abs_value * (scale as f32) + 0.5) as i64;

    let whole = scaled / scale;
    let frac = scaled % scale;

    let mut scratch = [0u8; 32];
    let len = write_fixed(&mut scratch, negative, whole, frac, decimals as usize);
    copy_terminated(buf, &scratch[..len])
}

fn write_fixed(
    scratch: &mut [u8],
    negative: bool,
    whole: i64,
    frac: i64,
    decimals: usize,
) -> usize {
    let mut pos = 0usize;
    if negative {
        scratch[pos] = b'-';
        pos += 1;
    }
    pos += write_unsigned_int(&mut scratch[pos..], whole);
    if decimals > 0 {
        scratch[pos] = b'.';
        pos += 1;
        pos += write_fixed_width_int(&mut scratch[pos..], frac, decimals);
    }
    pos
}

struct FixedSpec {
    percent: usize,
    decimals: i32,
    after: usize,
}

fn parse_precision(spec: &[u8], start: usize) -> (Option<i32>, usize) {
    if start >= spec.len() || spec[start] != b'.' {
        return (Some(1), start);
    }
    let mut j = start + 1;
    let mut n: i32 = 0;
    let mut digits = 0;
    while j < spec.len() && spec[j].is_ascii_digit() {
        n = n * 10 + i32::from(spec[j] - b'0');
        j += 1;
        digits += 1;
        if digits > 2 {
            return (None, j);
        }
    }
    (Some(n), j)
}

fn find_fixed_spec(spec: &[u8]) -> Option<FixedSpec> {
    let mut i = 0usize;
    while i < spec.len() {
        if spec[i] == b'%' && i + 1 < spec.len() && spec[i + 1] == b'%' {
            i += 2;
            continue;
        }
        if spec[i] != b'%' {
            i += 1;
            continue;
        }
        let (precision, j) = parse_precision(spec, i + 1);
        let Some(decimals) = precision else {
            i = j;
            continue;
        };
        if j < spec.len() && spec[j] == b'f' {
            return Some(FixedSpec {
                percent: i,
                decimals,
                after: j + 1,
            });
        }
        i = j + 1;
    }
    None
}

#[must_use]
pub fn format_from_spec(buf: &mut [u8], value: f32, spec: &[u8]) -> usize {
    format_from_spec_at(buf, value, spec, None)
}

#[must_use]
pub fn format_from_spec_with_decimals(
    buf: &mut [u8],
    value: f32,
    spec: &[u8],
    decimals: i32,
) -> usize {
    format_from_spec_at(buf, value, spec, Some(decimals))
}

#[must_use]
pub fn spec_decimals(spec: &[u8]) -> i32 {
    find_fixed_spec(spec).map_or(1, |found| found.decimals)
}

fn format_from_spec_at(buf: &mut [u8], value: f32, spec: &[u8], decimals: Option<i32>) -> usize {
    if buf.is_empty() {
        return 0;
    }
    let Some(found) = find_fixed_spec(spec) else {
        return format_fixed(buf, value, decimals.unwrap_or(1));
    };

    let mut number_scratch = [0u8; 32];
    let number_len_total = format_fixed(
        &mut number_scratch,
        value,
        decimals.unwrap_or(found.decimals),
    );
    let number = &number_scratch[..number_len_total];

    let prefix = &spec[..found.percent];
    let suffix = &spec[found.after..];
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

#[must_use]
pub fn format_general(buf: &mut [u8], value: f32, sig_digits: i32) -> usize {
    if buf.is_empty() {
        return 0;
    }
    let sig_digits = sig_digits.clamp(1, MAX_SIG_DIGITS);
    if let Some(len) = copy_non_finite(buf, value) {
        return len;
    }
    let decimals = (sig_digits - int_digit_count(value.abs())).clamp(0, MAX_DECIMALS);

    let mut scratch = [0u8; 32];
    let len = format_fixed(&mut scratch, value, decimals);
    if len >= scratch.len() - 1 {
        return copy_terminated(buf, &scratch[..find_nul(&scratch)]);
    }

    let end = if decimals > 0 {
        trim_fraction(&scratch, len)
    } else {
        len
    };
    copy_terminated(buf, &scratch[..end])
}

const MAX_INT_DIGITS: i32 = 12;

fn copy_non_finite(buf: &mut [u8], value: f32) -> Option<usize> {
    if value.is_nan() {
        return Some(copy_terminated(buf, b"nan"));
    }
    if value.is_infinite() {
        let text: &[u8] = if value < 0.0 {
            b"-inf"
        } else {
            b"inf"
        };
        return Some(copy_terminated(buf, text));
    }
    None
}

fn int_digit_count(abs: f32) -> i32 {
    if abs < 1.0 {
        return 1;
    }
    let mut scan = abs;
    let mut digits = 0;
    while scan >= 1.0 && digits < MAX_INT_DIGITS {
        scan /= 10.0;
        digits += 1;
    }
    digits
}

fn trim_fraction(scratch: &[u8], len: usize) -> usize {
    let mut end = len;
    while end > 0 && scratch[end - 1] == b'0' {
        end -= 1;
    }
    if end > 0 && scratch[end - 1] == b'.' {
        end -= 1;
    }
    end
}

#[inline]
fn find_nul(buf: &[u8]) -> usize {
    buf.iter().position(|&b| b == 0).unwrap_or(buf.len())
}

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
mod tests;
