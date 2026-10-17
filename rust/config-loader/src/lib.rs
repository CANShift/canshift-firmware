#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

#[must_use]
pub fn parse_major_version(version: &[u8]) -> Option<i32> {
    parse_leading_number(version).map(|(value, _)| value)
}

#[must_use]
pub fn parse_minor_version(version: &[u8]) -> Option<i32> {
    let (_, major_len) = parse_leading_number(version)?;
    let after_dot = version.get(major_len..)?.strip_prefix(b".")?;
    parse_leading_number(after_dot).map(|(value, _)| value)
}

fn parse_leading_number(digits: &[u8]) -> Option<(i32, usize)> {
    if !digits.first()?.is_ascii_digit() {
        return None;
    }

    let mut acc: i64 = 0;
    let mut len = 0usize;
    for &b in digits {
        if !b.is_ascii_digit() {
            break;
        }
        acc = acc.checked_mul(10)?.checked_add((b - b'0') as i64)?;
        if acc > i32::MAX as i64 {
            return None;
        }
        len += 1;
    }
    Some((acc as i32, len))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_single_digit() {
        assert_eq!(parse_major_version(b"3"), Some(3));
    }

    #[test]
    fn parses_dot_separated_takes_major() {
        assert_eq!(parse_major_version(b"1.0"), Some(1));
        assert_eq!(parse_major_version(b"12.34.56"), Some(12));
    }

    #[test]
    fn parses_multi_digit_major() {
        assert_eq!(parse_major_version(b"2024.1"), Some(2024));
    }

    #[test]
    fn rejects_empty() {
        assert_eq!(parse_major_version(b""), None);
    }

    #[test]
    fn rejects_leading_non_digit() {
        assert_eq!(parse_major_version(b"v1"), None);
        assert_eq!(parse_major_version(b".1"), None);
        assert_eq!(parse_major_version(b"a1"), None);
    }

    #[test]
    fn rejects_leading_whitespace() {
        assert_eq!(parse_major_version(b" 1"), None);
        assert_eq!(parse_major_version(b"\t1"), None);
    }

    #[test]
    fn rejects_leading_sign() {
        assert_eq!(parse_major_version(b"+1"), None);
        assert_eq!(parse_major_version(b"-1"), None);
    }

    #[test]
    fn parses_zero() {
        assert_eq!(parse_major_version(b"0"), Some(0));
        assert_eq!(parse_major_version(b"0.0.0"), Some(0));
    }

    #[test]
    fn rejects_overflow() {
        assert_eq!(parse_major_version(b"2147483648"), None);
        assert_eq!(parse_major_version(b"99999999999"), None);
    }

    #[test]
    fn accepts_i32_max() {
        assert_eq!(parse_major_version(b"2147483647"), Some(i32::MAX));
    }

    #[test]
    fn embedded_nul_after_digits_stops_at_nul() {
        let v = b"42\0extra";
        assert_eq!(parse_major_version(v), Some(42));
    }

    #[test]
    fn no_digits_at_all_after_first_check() {
        assert_eq!(parse_major_version(b"abc"), None);
    }

    #[test]
    fn minor_parses_second_component() {
        assert_eq!(parse_minor_version(b"1.42.0"), Some(42));
        assert_eq!(parse_minor_version(b"1.0.0"), Some(0));
        assert_eq!(parse_minor_version(b"12.24"), Some(24));
    }

    #[test]
    fn minor_rejects_missing_or_malformed_component() {
        assert_eq!(parse_minor_version(b"1"), None);
        assert_eq!(parse_minor_version(b"1."), None);
        assert_eq!(parse_minor_version(b"1.x"), None);
        assert_eq!(parse_minor_version(b"1-25"), None);
        assert_eq!(parse_minor_version(b""), None);
        assert_eq!(parse_minor_version(b"v1.25"), None);
    }

    #[test]
    fn minor_rejects_overflow() {
        assert_eq!(parse_minor_version(b"1.2147483648"), None);
    }
}
