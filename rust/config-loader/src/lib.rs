// config-loader — Rust port of pure-logic helpers from
// `canshift-firmware/src/config/config_loader.cpp` (#1177 R-10).
//
// Current surface: `parse_major_version` only. The companion
// `checkSchemaVersion` in the C++ is NOT pure (it calls `LOG_WARN`,
// `LOG_ERROR`, `ErrorStore::push`, `snprintf`) and stays C++ — it consumes
// this crate's parse result via the FFI shim.
//
// Why a dedicated crate for a 9-LoC parse: defending the version-mismatch
// decision is security-relevant. `checkSchemaVersion` is what stops the
// firmware from interpreting a future schema's `dashboard.json` with the
// current decoder, which historically (#203) was the path of least resistance
// for "weird config" bug reports. A typed Rust parse with explicit overflow
// + leading-whitespace + signed-input handling makes the rule legible and
// keeps the door open for additional config_loader helpers (parseWidgetType,
// parseSignalType, etc.) to land in the same crate without a new round of
// boilerplate.

#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

// Panic handler — required for `no_std + staticlib`. Halts forever (same
// strategy as the other ports). `parse_major_version` returns `None` on bad
// input rather than panicking; reaching the handler means an internal
// invariant break (e.g. checked_mul saturating panic if a digit run blew
// past i64 — currently unreachable but defended at the type level).
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

// Extract the major component of a "major.minor.patch" version string.
// Returns `None` (mapped to -1 in the FFI shim) when the string is empty,
// missing leading digits, or the parsed integer doesn't fit `i32`.
//
// Behaviour vs the C++ `parseMajorVersion` it replaces:
//
//   - empty string → None              (C++ returns -1 — equivalent)
//   - leading non-digit ("v1") → None  (C++ rejects via `end == version`)
//   - overflow > i32::MAX → None       (C++ rejects via `major > INT_MAX`)
//   - negative result → None           (C++ rejects via `major < 0`)
//   - trailing non-digits → first run of digits wins, e.g. "1.2.3" → Some(1)
//     (C++ `strtol` behaviour preserved — this is the production case)
//
// Tightened (intentional, documented) divergence from C++:
//
//   - leading whitespace ("  1") → None
//     C++ `strtol` skips ASCII whitespace before parsing; we reject.
//   - leading sign ("+1" / "-1") → None
//     C++ `strtol` accepts the sign; in practice "-1" is rejected anyway by
//     the `major < 0` guard, and "+1" was a hypothetical (the firmware only
//     ever sees canonical "1.0" / "2.3.1" strings from the JSON parser).
//
// The divergence is unreachable on the production wire — the JSON parser
// produces clean canonical strings — and is locked down by both this crate's
// unit tests and the `test_config_loader/` integration suite.
#[must_use]
pub fn parse_major_version(version: &[u8]) -> Option<i32> {
    if version.is_empty() {
        return None;
    }

    // Require the first byte to be a digit. C++ `strtol` would silently
    // accept leading whitespace and an optional `+`/`-` sign; we reject so
    // future callers can't drift the contract. The firmware only ever feeds
    // canonical version strings ("1.0", "2.3.1") so this divergence is
    // hypothetical and safe.
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
