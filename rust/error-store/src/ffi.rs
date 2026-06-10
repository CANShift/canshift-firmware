use core::ffi::c_char;
use core::slice;

use crate::{dismiss_at, get_all, push, FwError, CODE_LEN, MESSAGE_LEN};

const _: () = assert!(core::mem::size_of::<FwError>() == 1 + CODE_LEN + MESSAGE_LEN);

// Defends against a missing NUL on a hostile input.
const MAX_INPUT_LEN: usize = 64;

// Caller must hold the portMUX. See lib.rs::push.
// # Safety
// - `ring` points to `ring_size` writable entries.
// - `head`/`count`/`version` are writable.
// - `code`/`message` are NUL-terminated or null (capped at MAX_INPUT_LEN).
#[no_mangle]
pub unsafe extern "C" fn error_store_push_rs(
    ring: *mut FwError,
    ring_size: u8,
    head: *mut u8,
    count: *mut u8,
    version: *mut u32,
    source: u8,
    code: *const c_char,
    message: *const c_char,
) {
    if ring.is_null()
        || head.is_null()
        || count.is_null()
        || version.is_null()
        || ring_size == 0
    {
        return;
    }
    let ring_slice = unsafe { slice::from_raw_parts_mut(ring, ring_size as usize) };
    let code_bytes = unsafe { c_str_bounded(code, MAX_INPUT_LEN) };
    let message_bytes = unsafe { c_str_bounded(message, MAX_INPUT_LEN) };
    push(
        ring_slice,
        unsafe { &mut *head },
        unsafe { &mut *count },
        unsafe { &mut *version },
        source,
        code_bytes,
        message_bytes,
    );
}

// # Safety: caller must hold portMUX; `ring` readable, `out` writable for max_count.
#[no_mangle]
pub unsafe extern "C" fn error_store_get_all_rs(
    ring: *const FwError,
    ring_size: u8,
    head: u8,
    count: u8,
    out: *mut FwError,
    max_count: u8,
) -> u8 {
    if ring.is_null() || out.is_null() || ring_size == 0 || max_count == 0 {
        return 0;
    }
    let ring_slice = unsafe { slice::from_raw_parts(ring, ring_size as usize) };
    let out_slice = unsafe { slice::from_raw_parts_mut(out, max_count as usize) };
    get_all(ring_slice, head, count, out_slice)
}

// # Safety: same as error_store_push_rs. Out-of-range row is a no-op.
#[no_mangle]
pub unsafe extern "C" fn error_store_dismiss_at_rs(
    ring: *mut FwError,
    ring_size: u8,
    head: *mut u8,
    count: *mut u8,
    version: *mut u32,
    row: u8,
) {
    if ring.is_null()
        || head.is_null()
        || count.is_null()
        || version.is_null()
        || ring_size == 0
    {
        return;
    }
    let ring_slice = unsafe { slice::from_raw_parts_mut(ring, ring_size as usize) };
    dismiss_at(
        ring_slice,
        unsafe { &mut *head },
        unsafe { &mut *count },
        unsafe { &mut *version },
        row,
    );
}

unsafe fn c_str_bounded<'a>(ptr: *const c_char, cap: usize) -> &'a [u8] {
    if ptr.is_null() {
        return &[];
    }
    let mut len = 0usize;
    while len < cap {
        if unsafe { *(ptr as *const u8).add(len) } == 0 {
            break;
        }
        len += 1;
    }
    unsafe { slice::from_raw_parts(ptr as *const u8, len) }
}

#[cfg(test)]
mod tests {
    use super::*;

    const RING: u8 = 6;

    fn blank_ring() -> [FwError; RING as usize] {
        [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; RING as usize]
    }

    fn cstr(s: &[u8]) -> Vec<u8> {
        let mut v = s.to_vec();
        v.push(0);
        v
    }

    #[test]
    fn ffi_push_then_get_all() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;
        let code = cstr(b"A");
        let msg = cstr(b"first");

        unsafe {
            error_store_push_rs(
                ring.as_mut_ptr(),
                RING,
                &mut head,
                &mut count,
                &mut version,
                1,
                code.as_ptr() as *const c_char,
                msg.as_ptr() as *const c_char,
            );
        }
        assert_eq!(count, 1);
        assert_eq!(version, 1);

        let mut out = blank_ring();
        let n = unsafe {
            error_store_get_all_rs(
                ring.as_ptr(),
                RING,
                head,
                count,
                out.as_mut_ptr(),
                RING,
            )
        };
        assert_eq!(n, 1);
        assert_eq!(&out[0].code[..1], b"A");
    }

    #[test]
    fn ffi_push_null_ring_is_noop() {
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;
        let code = cstr(b"A");
        let msg = cstr(b"x");
        unsafe {
            error_store_push_rs(
                core::ptr::null_mut(),
                RING,
                &mut head,
                &mut count,
                &mut version,
                1,
                code.as_ptr() as *const c_char,
                msg.as_ptr() as *const c_char,
            );
        }
        assert_eq!(count, 0);
        assert_eq!(version, 0);
    }

    #[test]
    fn ffi_push_null_code_is_treated_as_empty() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;
        let msg = cstr(b"x");
        unsafe {
            error_store_push_rs(
                ring.as_mut_ptr(),
                RING,
                &mut head,
                &mut count,
                &mut version,
                1,
                core::ptr::null(),
                msg.as_ptr() as *const c_char,
            );
        }
        assert_eq!(count, 1);
        assert_eq!(ring[0].code[0], 0);
    }

    #[test]
    fn ffi_dismiss_at_oldest_advances_head() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;
        for i in 0..3 {
            let code = cstr(&[b'A' + i]);
            unsafe {
                error_store_push_rs(
                    ring.as_mut_ptr(),
                    RING,
                    &mut head,
                    &mut count,
                    &mut version,
                    1,
                    code.as_ptr() as *const c_char,
                    cstr(b"").as_ptr() as *const c_char,
                );
            }
        }
        unsafe {
            error_store_dismiss_at_rs(
                ring.as_mut_ptr(),
                RING,
                &mut head,
                &mut count,
                &mut version,
                2,
            );
        }
        assert_eq!(count, 2);
        assert_eq!(head, 1);
    }
}
