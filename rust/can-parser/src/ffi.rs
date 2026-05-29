//! C ABI bridge for the can-parser crate.
//!
//! Exposes a single `extern "C"` entry point so `can_parser.cpp` can delegate
//! `detail::decodeBytes` to the Rust port when `USE_RUST_CAN_PARSER=1` is set
//! in the PlatformIO build. The signature mirrors the C++ original byte-for-
//! byte so the call sites don't change.
//!
//! Memory ownership:
//!   - No state — the function is stateless arithmetic over a borrowed slice.
//!   - The input pointer is read-only, never aliased, never freed by Rust.
//!
//! Why not `bindgen`-generated headers? The FFI surface is one function over
//! primitive types; a hand-written `can_parser_rs.h` is ~25 lines and keeps
//! `libclang` + `build.rs` out of the firmware build.

use crate::{decode_bytes, CAN_FRAME_MAX_BYTES};
use core::slice;

/// Resolve a CAN payload byte range to a decoded `f32`. C ABI mirror of
/// `CanParser::detail::decodeBytes`.
///
/// # Safety
/// Caller MUST guarantee that `data` points to a readable buffer of at
/// least `CAN_FRAME_MAX_BYTES` (8) bytes. The C++ contract already does —
/// every call site passes either a fixed-size frame or a `kCanFrameMaxBytes`
/// buffer.
#[no_mangle]
pub unsafe extern "C" fn decode_bytes_rs(
    data: *const u8,
    start_byte: u8,
    byte_len: u8,
    big_endian: bool,
    is_signed: bool,
    bit_mask: u8,
    scale: f32,
    offset: f32,
) -> f32 {
    if data.is_null() {
        return 0.0;
    }
    // SAFETY: caller contract guarantees `data` is readable for
    // CAN_FRAME_MAX_BYTES bytes (mirrors kCanFrameMaxBytes in C++). The
    // `decode_bytes` body bounds-checks `start + byte_len` against this
    // length before any index read, so a malformed config still returns
    // 0.0 without ever indexing past the slice.
    let slice = unsafe { slice::from_raw_parts(data, CAN_FRAME_MAX_BYTES) };
    decode_bytes(slice, start_byte, byte_len, big_endian, is_signed, bit_mask, scale, offset)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_matches_native_for_known_fixture() {
        let frame = [0x34, 0x12, 0, 0, 0, 0, 0, 0];
        // SAFETY: 8-byte buffer matches the kCanFrameMaxBytes contract.
        let r = unsafe { decode_bytes_rs(frame.as_ptr(), 0, 2, false, false, 0, 0.1, 0.0) };
        assert!((r - 466.0).abs() < 1e-4, "got {r}");
    }

    #[test]
    fn ffi_null_data_returns_zero() {
        // SAFETY: explicitly passing a null pointer is the contract.
        let r = unsafe { decode_bytes_rs(core::ptr::null(), 0, 1, false, false, 0, 1.0, 0.0) };
        assert_eq!(r, 0.0);
    }
}
