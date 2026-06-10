// Rust port of pure-logic helpers from config_loader.cpp (#1177 R-10).
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

/// Tightened vs C++ strtol: rejects leading whitespace and `+/-` signs.
/// Production strings ("1.0", "2.3.1") never trigger the divergence.
#[must_use]
pub fn parse_major_version(version: &[u8]) -> Option<i32> {
    if version.is_empty() {
        return None;
    }

    if !version[0].is_ascii_digit() {
        return None;
    }

    let mut acc: i64 = 0;
    for &b in version {
        if !b.is_ascii_digit() {
            break;
        }
        acc = acc.checked_mul(10)?.checked_add((b - b'0') as i64)?;
        if acc > i32::MAX as i64 {
            return None;
        }
    }
    Some(acc as i32)
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
        // i32::MAX is 2_147_483_647
        assert_eq!(parse_major_version(b"2147483648"), None);
        assert_eq!(parse_major_version(b"99999999999"), None);
    }

    #[test]
    fn accepts_i32_max() {
        assert_eq!(parse_major_version(b"2147483647"), Some(i32::MAX));
    }

    #[test]
    fn embedded_nul_after_digits_stops_at_nul() {
        // C strings would stop here too — `strtol` reads until a non-digit.
        let v = b"42\0extra";
        assert_eq!(parse_major_version(v), Some(42));
    }

    #[test]
    fn no_digits_at_all_after_first_check() {
        // Unreachable via the first-byte gate, but defensive.
        assert_eq!(parse_major_version(b"abc"), None);
    }
}
