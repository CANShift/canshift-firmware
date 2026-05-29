// ffi.rs — C ABI shim for the config-loader crate.
//
// One function exposed: `parse_major_version_rs`. Takes a C string pointer
// and returns `i32`, mapping `Option<i32>::None` to `-1` so the C++ caller
// (`canshift-firmware/src/config/config_loader.cpp::parseMajorVersion`) sees
// the same sentinel it always used. `checkSchemaVersion` is NOT ported — it
// stays C++ because it has `LOG_*` / `ErrorStore::push` / `snprintf` side
// effects that are out of scope for pure-logic Rust modules per #1177.

use core::ffi::c_char;
use core::slice;

use crate::parse_major_version;

// Defensive cap on how many bytes we scan looking for the NUL terminator.
// CONFIG_SCHEMA_VERSION strings on the wire are like "1.0" / "2.3.1" — never
// more than a few bytes. A bounded scan means a malformed input without a
// NUL byte (corrupted JSON parse, etc.) can't run off the end of the
// buffer. 32 bytes is double what the longest realistic semver could need.
const MAX_VERSION_LEN: usize = 32;

// Mirror of `parseMajorVersion` from `config_loader.cpp`. Returns the major
// component of a version string, or `-1` on any failure (matches the C++
// sentinel). See `parse_major_version` in `lib.rs` for the exact contract.
//
// # Safety
// `version` must either be null or point to a NUL-terminated C string whose
// readable length (including the terminator) is at most `MAX_VERSION_LEN`
// bytes. Strings longer than the cap are rejected (-1) without reading
// past the cap.
#[no_mangle]
pub unsafe extern "C" fn parse_major_version_rs(version: *const c_char) -> i32 {
    if version.is_null() {
        return -1;
    }

    // Bounded NUL scan so a missing terminator can't UAF.
    let bytes = version as *const u8;
    let mut len = 0usize;
    while len < MAX_VERSION_LEN {
        if unsafe { *bytes.add(len) } == 0 {
            break;
        }
        len += 1;
    }
    if len == MAX_VERSION_LEN {
        // No NUL found within the cap — treat as malformed input. Production
        // version strings never exceed a handful of bytes.
        return -1;
    }

    let slice = unsafe { slice::from_raw_parts(bytes, len) };
    parse_major_version(slice).unwrap_or(-1)
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr;

    fn c_str(s: &[u8]) -> Vec<u8> {
        let mut v = s.to_vec();
        v.push(0);
        v
    }

    #[test]
    fn ffi_basic_parse() {
        let s = c_str(b"1.0.2");
        unsafe {
            assert_eq!(parse_major_version_rs(s.as_ptr() as *const c_char), 1);
        }
    }

    #[test]
    fn ffi_null_returns_minus_one() {
        unsafe {
            assert_eq!(parse_major_version_rs(ptr::null()), -1);
        }
    }

    #[test]
    fn ffi_empty_returns_minus_one() {
        let s = c_str(b"");
        unsafe {
            assert_eq!(parse_major_version_rs(s.as_ptr() as *const c_char), -1);
        }
    }

    #[test]
    fn ffi_overflow_returns_minus_one() {
        let s = c_str(b"99999999999");
        unsafe {
            assert_eq!(parse_major_version_rs(s.as_ptr() as *const c_char), -1);
        }
    }

    #[test]
    fn ffi_no_nul_within_cap_returns_minus_one() {
        // 32 bytes of digits with NO trailing NUL within the cap. The FFI
        // must NOT walk past byte 32.
        let buf = vec![b'1'; MAX_VERSION_LEN + 8];
        unsafe {
            assert_eq!(parse_major_version_rs(buf.as_ptr() as *const c_char), -1);
        }
    }

    #[test]
    fn ffi_canonical_schema_versions() {
        // Smoke: representative production inputs that the firmware actually
        // ships with — must round-trip to the expected major.
        for (s, expected) in &[
            (&b"1.0"[..], 1),
            (&b"2"[..], 2),
            (&b"10.20.30"[..], 10),
            (&b"99.0.0"[..], 99), // the test_config_loader mismatch fixture
        ] {
            let cs = c_str(s);
            unsafe {
                assert_eq!(parse_major_version_rs(cs.as_ptr() as *const c_char), *expected);
            }
        }
    }
}
