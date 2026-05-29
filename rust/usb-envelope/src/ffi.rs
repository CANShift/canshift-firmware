// ffi.rs — C ABI shim for the usb-envelope crate. Mirrors the C++ surface in
// `canshift-firmware/src/hal/usb/usb_envelope.h` so the existing caller and
// Unity tests link unchanged when `USE_RUST_USB_ENVELOPE=1` is set.
//
// Both functions take raw pointers + lengths from C and return pointers that
// index into the caller's input buffer. The Rust impl computes offsets; the
// shim adds them to the input base pointer. No bytes are copied and no
// allocation happens — the buffer's lifetime is the caller's problem (same
// as the C++ original).

use core::ptr;
use core::slice;

use crate::{find_needle, find_payload_slice};

// Returns a pointer into `haystack` at the first occurrence of `needle`, or
// `null` if no match. Matches `UsbEnvelope::findNeedle` byte-for-byte:
//   - `needle_len == 0` returns null.
//   - `haystack_len < needle_len` returns null.
//   - Either pointer being null also returns null (defensive — the C++
//     version dereferenced; we tighten the contract).
//
// # Safety
// `haystack` must point to a readable buffer of at least `haystack_len`
// bytes (or be null), and `needle` to one of at least `needle_len` bytes
// (or be null). The returned pointer aliases into `haystack` and is only
// valid while the caller's buffer is live.
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

// Locate the `"payload"` value object inside a top-level JSON envelope.
// On success: writes the slice length into `*out_len` and returns a pointer
// to the opening `{`. On failure: writes 0 into `*out_len` (when non-null)
// and returns `null`.
//
// Mirrors `UsbEnvelope::findPayloadSlice` — see the C++ header for the full
// contract. The Rust impl ignores embedded NULs by design (length-bounded
// scan).
//
// # Safety
// `json_line` must point to a readable buffer of at least `line_len` bytes
// (or be null). `out_len` may be null. The returned pointer aliases into
// `json_line` and is only valid while the caller's buffer is live.
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
            assert_eq!(out_len, 7); // {"a":1}
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
