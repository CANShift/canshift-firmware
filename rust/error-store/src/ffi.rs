// ffi.rs — C ABI shim for the error-store crate. The C++ wrapper in
// `error_store.cpp` calls these symbols from INSIDE the portMUX critical
// section — Rust assumes exclusive access and never tries to take a lock
// of its own.
//
// All three exported functions take raw pointers to the C++-owned ring
// buffer and state globals. The shim rebuilds Rust slices + references
// over those pointers and dispatches to the safe lib.rs functions.

use core::ffi::c_char;
use core::slice;

use crate::{dismiss_at, get_all, push, FwError, CODE_LEN, MESSAGE_LEN};

// Compile-time guard against drift between the C++ `FwError` layout
// (`uint8_t source; char code[12]; char message[52];` = 65 bytes) and the
// Rust mirror. If the C side ever bumps either field width, this fires at
// crate build time.
const _: () = assert!(core::mem::size_of::<FwError>() == 1 + CODE_LEN + MESSAGE_LEN);

// Length cap for the FFI strncpy-equivalent. C strings on this path are
// `<=52` bytes long in practice; cap at 64 so we don't walk past the end
// of a malformed input that lacks a NUL.
const MAX_INPUT_LEN: usize = 64;

// `ErrorStore::push` core. Caller must hold the portMUX. Mutates the ring,
// `head`, `count`, `version` in place. See `lib.rs::push` for the contract.
//
// # Safety
// - `ring` points to `ring_size` writable `FwError` entries.
// - `head`, `count`, `version` point to writable storage.
// - `code` / `message` are NUL-terminated C strings (or null). Reads are
//   capped at `MAX_INPUT_LEN` regardless to defend against a missing
//   terminator on a hostile input.
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

// `ErrorStore::getAll` core. Caller must hold the portMUX. Read-only on the
// ring + state; writes up to `max_count` entries into `out` (newest first)
// and returns the number written.
//
// # Safety
// - `ring` points to `ring_size` readable `FwError` entries.
// - `out` points to at least `max_count` writable `FwError` entries.
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

// `ErrorStore::dismissAt` core. Caller must hold the portMUX. Mutates the
// ring + state. Out-of-range `row` is a silent no-op.
//
// # Safety
// Same as `error_store_push_rs`.
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

// Walk a NUL-terminated C string, capping at `cap` bytes so a missing
// terminator can't run off the end of the readable region.
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
        // Pushed with empty code — the entry exists but its code field is
        // all-zero (NUL-only).
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
        // row = count - 1 = 2 → oldest
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
