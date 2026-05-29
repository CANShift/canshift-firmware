//! C ABI bridge for the signal-map crate.
//!
//! Exposes a single `extern "C"` entry point so `signal_map.cpp` can delegate
//! to the Rust lookup when `USE_RUST_SIGNAL_MAP=1` is set in the PlatformIO
//! build. The C++ caller passes a NUL-terminated `const char *`; we walk it
//! to find the NUL (bounded by `MAX_NAME_LEN`, defensive against missing
//! terminator) and then look the name up in the static table.
//!
//! Memory ownership:
//!   - No state — the lookup table is `&'static`, no allocations, no slots.
//!   - The input pointer is read-only and never aliased.
//!
//! Why not `bindgen`-generated headers? The FFI surface is one function
//! taking a `*const c_char` and returning a `u8`; a hand-written
//! `signal_map_rs.h` is ~25 lines and avoids dragging `libclang` + a
//! `build.rs` into the firmware build.

use crate::{signal_id_from_name, SignalId, SIGNAL_COUNT};
use core::slice;

/// Defensive upper bound on the C string we'll walk before giving up. Every
/// known signal name fits in 16 chars; doubling that leaves headroom for
/// the future without inviting unbounded reads through a missing NUL.
const MAX_NAME_LEN: usize = 32;

/// Resolve a NUL-terminated C string to a `SignalId`. Returns `SIGNAL_COUNT`
/// for:
///   - `name` is null
///   - `name` is empty (first byte already NUL)
///   - `name` is not NUL-terminated within `MAX_NAME_LEN` bytes (defensive
///     guard the C++ original lacks — Rust slice access stays in-bounds)
///   - `name` is not a known signal
///
/// # Safety
/// Caller MUST guarantee that `name` points to a readable, NUL-terminated
/// C string. The function bounds the read at `MAX_NAME_LEN` so a missing
/// NUL is contained, but a caller passing freed memory still wins the
/// undefined-behaviour lottery.
#[no_mangle]
pub unsafe extern "C" fn signal_id_from_name_rs(name: *const core::ffi::c_char) -> SignalId {
    if name.is_null() {
        return SIGNAL_COUNT;
    }
    // SAFETY: caller guarantees `name` points to a readable C string. We
    // bound the read at MAX_NAME_LEN so a missing NUL terminator can't run
    // off into adjacent memory — return SIGNAL_COUNT for "too long",
    // matching the C++ "unknown" semantics for malformed input.
    let bytes = unsafe { slice::from_raw_parts(name.cast::<u8>(), MAX_NAME_LEN) };
    let len = match bytes.iter().position(|&b| b == 0) {
        Some(n) => n,
        None => return SIGNAL_COUNT,
    };
    if len == 0 {
        return SIGNAL_COUNT;
    }
    // Sub-slice excluding the NUL — we already located it above.
    match core::str::from_utf8(&bytes[..len]) {
        Ok(s) => signal_id_from_name(s),
        // Non-UTF8 names cannot match any of our ASCII keys; return the
        // sentinel rather than panic in the FFI path.
        Err(_) => SIGNAL_COUNT,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ids;
    use core::ffi::CStr;

    fn lookup_cstr(s: &CStr) -> SignalId {
        // SAFETY: CStr always exposes a valid NUL-terminated pointer.
        unsafe { signal_id_from_name_rs(s.as_ptr()) }
    }

    #[test]
    fn known_name_resolves() {
        let s = CStr::from_bytes_with_nul(b"rpm\0").unwrap();
        assert_eq!(lookup_cstr(s), ids::RPM);
    }

    #[test]
    fn null_returns_sentinel() {
        // SAFETY: explicitly passing null is the contract.
        let id = unsafe { signal_id_from_name_rs(core::ptr::null()) };
        assert_eq!(id, SIGNAL_COUNT);
    }

    #[test]
    fn empty_returns_sentinel() {
        let s = CStr::from_bytes_with_nul(b"\0").unwrap();
        assert_eq!(lookup_cstr(s), SIGNAL_COUNT);
    }

    #[test]
    fn unknown_returns_sentinel() {
        let s = CStr::from_bytes_with_nul(b"not_a_signal\0").unwrap();
        assert_eq!(lookup_cstr(s), SIGNAL_COUNT);
    }
}
