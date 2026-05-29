// ffi.rs — C ABI shim for the format-float crate. Mirrors the three public
// `FloatFormat::*` entry points in `canshift-firmware/src/util/format_float.h`
// so the existing callers (top bar, widget helpers, BLE server, USB telemetry)
// and the new Unity parity suite both link unchanged when
// `USE_RUST_FORMAT_FLOAT=1` is set.
//
// All three functions take a raw `*mut c_char` output buffer, a `usize`
// capacity, a `f32` value, and a third argument that varies per function. They
// return a `usize` matching snprintf semantics — the number of characters that
// WOULD have been written (excluding the NUL).

use core::ffi::{c_char, CStr};
use core::slice;

use crate::{format_fixed, format_from_spec, format_general};

// `formatFixed(buf, size, value, decimals)` — see the lib.rs comment for the
// behaviour contract.
//
// # Safety
// `buf` must point to a writable buffer of at least `size` bytes (or be
// null). If null or `size == 0`, the call returns 0.
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

// `formatFromSpec(buf, size, value, spec)` — see lib.rs.
//
// `spec` is a NUL-terminated C string. A null `spec` is treated as empty
// (the C++ early-returns with `formatFixedSigned(... 1)`).
//
// # Safety
// `buf` must point to a writable buffer of at least `size` bytes (or be
// null). `spec` must either be null or a valid NUL-terminated C string.
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
        // Match the C++ `spec[0] == '\0'` early-return path — render
        // `%.1f` of the value, ignoring the spec.
        return format_from_spec(out, value, &[]);
    }
    let spec_slice = unsafe { CStr::from_ptr(spec) }.to_bytes();
    format_from_spec(out, value, spec_slice)
}

// `formatGeneral(buf, size, value, sig_digits)` — see lib.rs.
//
// # Safety
// `buf` must point to a writable buffer of at least `size` bytes (or be
// null).
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
