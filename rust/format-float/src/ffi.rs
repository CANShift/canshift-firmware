use core::ffi::{c_char, CStr};
use core::slice;

use crate::{format_fixed, format_from_spec, format_general};

// # Safety: `buf` writable for `size` bytes (or null → returns 0).
#[no_mangle]
pub unsafe extern "C" fn format_fixed_rs(
    buf: *mut c_char,
    size: usize,
    value: f32,
    decimals: i32,
) -> usize {
    if buf.is_null() || size == 0 {
        return 0;
    }
    let out = unsafe { slice::from_raw_parts_mut(buf as *mut u8, size) };
    format_fixed(out, value, decimals)
}

// # Safety: `buf` writable for `size`; `spec` null or NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn format_from_spec_rs(
    buf: *mut c_char,
    size: usize,
    value: f32,
    spec: *const c_char,
) -> usize {
    if buf.is_null() || size == 0 {
        return 0;
    }
    let out = unsafe { slice::from_raw_parts_mut(buf as *mut u8, size) };
    if spec.is_null() {
        return format_from_spec(out, value, &[]);
    }
    let spec_slice = unsafe { CStr::from_ptr(spec) }.to_bytes();
    format_from_spec(out, value, spec_slice)
}

// # Safety: `buf` writable for `size` bytes (or null → returns 0).
#[no_mangle]
pub unsafe extern "C" fn format_general_rs(
    buf: *mut c_char,
    size: usize,
    value: f32,
    sig_digits: i32,
) -> usize {
    if buf.is_null() || size == 0 {
        return 0;
    }
    let out = unsafe { slice::from_raw_parts_mut(buf as *mut u8, size) };
    format_general(out, value, sig_digits)
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr;

    fn as_str(buf: &[u8]) -> &str {
        let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
        core::str::from_utf8(&buf[..end]).unwrap()
    }

    #[test]
    fn ffi_fixed_basic() {
        let mut buf = [0u8; 16];
        unsafe {
            let n = format_fixed_rs(buf.as_mut_ptr() as *mut c_char, buf.len(), 2.71, 2);
            assert_eq!(n, 4);
            assert_eq!(as_str(&buf), "2.71");
        }
    }

    #[test]
    fn ffi_fixed_null_buf_returns_zero() {
        unsafe {
            assert_eq!(format_fixed_rs(ptr::null_mut(), 16, 1.0, 2), 0);
        }
    }

    #[test]
    fn ffi_spec_with_prefix_suffix() {
        let mut buf = [0u8; 16];
        let spec = b"%.1fV\0";
        unsafe {
            format_from_spec_rs(
                buf.as_mut_ptr() as *mut c_char,
                buf.len(),
                12.3,
                spec.as_ptr() as *const c_char,
            );
        }
        assert_eq!(as_str(&buf), "12.3V");
    }

    #[test]
    fn ffi_spec_null_falls_back_to_one_decimal() {
        let mut buf = [0u8; 16];
        unsafe {
            format_from_spec_rs(buf.as_mut_ptr() as *mut c_char, buf.len(), 5.67, ptr::null());
        }
        assert_eq!(as_str(&buf), "5.7");
    }

    #[test]
    fn ffi_general_strips_trailing_zeros() {
        let mut buf = [0u8; 16];
        unsafe {
            format_general_rs(buf.as_mut_ptr() as *mut c_char, buf.len(), 2.0, 4);
        }
        assert_eq!(as_str(&buf), "2");
    }
}
