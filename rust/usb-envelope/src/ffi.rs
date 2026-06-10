use core::ptr;
use core::slice;

use crate::{find_needle, find_payload_slice};

/// # Safety: both pointers null OR readable for their length. Tightens vs C++ —
/// null returns null instead of dereferencing.
#[no_mangle]
pub unsafe extern "C" fn find_needle_rs(
    haystack: *const u8,
    haystack_len: usize,
    needle: *const u8,
    needle_len: usize,
) -> *const u8 {
    if haystack.is_null() || needle.is_null() {
        return ptr::null();
    }
    let hay = unsafe { slice::from_raw_parts(haystack, haystack_len) };
    let ndl = unsafe { slice::from_raw_parts(needle, needle_len) };
    match find_needle(hay, ndl) {
        Some(off) => unsafe { haystack.add(off) },
        None => ptr::null(),
    }
}

/// # Safety: `json_line` null or readable for `line_len`; `out_len` may be null.
#[no_mangle]
pub unsafe extern "C" fn find_payload_slice_rs(
    json_line: *const u8,
    line_len: usize,
    out_len: *mut usize,
) -> *const u8 {
    if json_line.is_null() || out_len.is_null() {
        return ptr::null();
    }
    unsafe { *out_len = 0 };
    let line = unsafe { slice::from_raw_parts(json_line, line_len) };
    match find_payload_slice(line) {
        Some((off, len)) => {
            unsafe { *out_len = len };
            unsafe { json_line.add(off) }
        }
        None => ptr::null(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_find_needle_hits() {
        let hay = b"abc_payload_def";
        let ndl = b"payload";
        unsafe {
            let p = find_needle_rs(hay.as_ptr(), hay.len(), ndl.as_ptr(), ndl.len());
            assert!(!p.is_null());
            assert_eq!(p.offset_from(hay.as_ptr()), 4);
        }
    }

    #[test]
    fn ffi_find_needle_null_inputs() {
        unsafe {
            assert!(find_needle_rs(ptr::null(), 5, b"x".as_ptr(), 1).is_null());
            assert!(find_needle_rs(b"hay".as_ptr(), 3, ptr::null(), 1).is_null());
        }
    }

    #[test]
    fn ffi_payload_writes_length_and_returns_pointer() {
        let line = b"{\"payload\":{\"a\":1}}";
        let mut out_len: usize = 99;
        unsafe {
            let p = find_payload_slice_rs(line.as_ptr(), line.len(), &mut out_len);
            assert!(!p.is_null());
            assert_eq!(out_len, 7);
            let got = slice::from_raw_parts(p, out_len);
            assert_eq!(got, b"{\"a\":1}");
        }
    }

    #[test]
    fn ffi_payload_clears_out_len_on_miss() {
        let line = b"{\"other\":1}";
        let mut out_len: usize = 42;
        unsafe {
            let p = find_payload_slice_rs(line.as_ptr(), line.len(), &mut out_len);
            assert!(p.is_null());
            assert_eq!(out_len, 0);
        }
    }

    #[test]
    fn ffi_payload_null_json_or_outlen() {
        let mut out_len: usize = 0;
        unsafe {
            assert!(find_payload_slice_rs(ptr::null(), 10, &mut out_len).is_null());
            assert!(find_payload_slice_rs(b"{\"payload\":{}}".as_ptr(), 14, ptr::null_mut())
                .is_null());
        }
    }
}
