use core::ffi::c_char;
use core::slice;

use crate::parse_major_version;

const MAX_VERSION_LEN: usize = 32;

/// Returns -1 on null/empty/no-NUL/overflow/unparseable.
/// # Safety: `version` null or NUL-terminated within MAX_VERSION_LEN.
#[no_mangle]
pub unsafe extern "C" fn parse_major_version_rs(version: *const c_char) -> i32 {
    if version.is_null() {
        return -1;
    }

    let bytes = version as *const u8;
    let mut len = 0usize;
    while len < MAX_VERSION_LEN {
        if unsafe { *bytes.add(len) } == 0 {
            break;
        }
        len += 1;
    }
    if len == MAX_VERSION_LEN {
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
        let buf = vec![b'1'; MAX_VERSION_LEN + 8];
        unsafe {
            assert_eq!(parse_major_version_rs(buf.as_ptr() as *const c_char), -1);
        }
    }

    #[test]
    fn ffi_canonical_schema_versions() {
        for (s, expected) in &[
            (&b"1.0"[..], 1),
            (&b"2"[..], 2),
            (&b"10.20.30"[..], 10),
            (&b"99.0.0"[..], 99),
        ] {
            let cs = c_str(s);
            unsafe {
                assert_eq!(parse_major_version_rs(cs.as_ptr() as *const c_char), *expected);
            }
        }
    }
}
