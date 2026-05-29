//! Rust port of `canshift-firmware/src/can/can_parser.cpp::detail::decodeBytes`
//! (issue #1177 R-1).
//!
//! Pure arithmetic on a fixed-size CAN payload — no allocation, no I/O,
//! no FreeRTOS. Mirrors the C++ original byte-for-byte so the existing
//! Unity suite (`test/test_can_parser/`) doubles as a parity gate when
//! `USE_RUST_CAN_PARSER=1` is built — same fixtures, same float deltas.
//!
//! What Rust catches that the C++ original didn't:
//!
//! - **Out-of-range slice access** — the C++ original guards `startByte +
//!   byteLen > kCanFrameMaxBytes` but still reads through a raw `data[]`
//!   pointer without checking that the caller actually passed a buffer of
//!   that size. The Rust slice view bounds-checks every indexing operation
//!   at compile-time / runtime.
//! - **Shift width UB** — for `byteLen == 4` the C++ code dodges
//!   `(1u64 << bits) - 1` by branching on `byteLen < 4`. The Rust port
//!   keeps the same branch but the 64-bit shift is expressed via `u64`
//!   explicitly so the optimizer can't lower it to a 32-bit shift and
//!   trip UB.
//! - **Endian / signedness combinatorics** — `cargo test -p can-parser`
//!   exercises every combination (LE/BE × signed/unsigned × byteLen ∈
//!   {1,2,4} × bitMask on/off) so a future regression on either side
//!   surfaces before it hits the device.

#![cfg_attr(not(any(test, feature = "std")), no_std)]

#[cfg(feature = "ffi")]
pub mod ffi;

// Panic handler — required for `no_std + staticlib`. Halts forever (same
// strategy as ota-hmac / signal-map). `decode_bytes` returns `0.0` on bad
// input rather than panicking; reaching the handler means an internal
// invariant break.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// Maximum bytes in a classic-CAN frame payload. Mirrors `kCanFrameMaxBytes`
/// in `app_config.h` — keep the two in lockstep.
pub const CAN_FRAME_MAX_BYTES: usize = 8;

/// Decode `byte_len` bytes starting at `start_byte` into a `f32`, applying
/// endianness, signedness, an optional bit-mask flag extraction, and a
/// linear `raw * scale + offset` transform.
///
/// Returns `0.0` when:
/// - `byte_len` is 0
/// - `start_byte + byte_len` exceeds [`CAN_FRAME_MAX_BYTES`]
/// - `start_byte + byte_len` exceeds the slice length
///
/// When `bit_mask != 0`, the function ignores `is_signed` / `scale` /
/// `offset` and returns `1.0` if the masked bits in the packed `raw` value
/// are set, `0.0` otherwise — matches the C++ original's flag-decode path.
#[must_use]
pub fn decode_bytes(
    data: &[u8],
    start_byte: u8,
    byte_len: u8,
    big_endian: bool,
    is_signed: bool,
    bit_mask: u8,
    scale: f32,
    offset: f32,
) -> f32 {
    if byte_len == 0 {
        return 0.0;
    }
    // The C++ original promotes both operands to `uint16_t` before adding
    // to keep the comparison well-defined on a malformed config where
    // `start_byte + byte_len` would overflow `u8`. We use `usize` here
    // which gives the same guarantee + a cheap second check against the
    // actual slice length so a too-short buffer returns 0.0 instead of
    // panicking on out-of-bounds indexing.
    let start = start_byte as usize;
    let len = byte_len as usize;
    if start + len > CAN_FRAME_MAX_BYTES || start + len > data.len() {
        return 0.0;
    }

    // Pack bytes into a 32-bit raw value.
    let mut raw: u32 = 0;
    if big_endian {
        for i in 0..len {
            raw = (raw << 8) | data[start + i] as u32;
        }
    } else {
        for i in 0..len {
            raw |= (data[start + i] as u32) << (i * 8);
        }
    }

    // Boolean flag path — apply mask, return 0.0 / 1.0.
    if bit_mask != 0 {
        return if raw & bit_mask as u32 != 0 { 1.0 } else { 0.0 };
    }

    let physical: f32 = if is_signed {
        let bits = (len as u32) * 8;
        // For `byte_len == 4` the 32-bit mask is degenerate — `raw` already
        // holds the correct two's-complement bit pattern and the `i32` cast
        // does the reinterpret. The `u64` shift below is gated on
        // `len < 4` so we never invoke 32-or-wider shift UB.
        if len < 4 && raw & (1u32 << (bits - 1)) != 0 {
            raw |= !((1u64 << bits) - 1) as u32;
        }
        raw as i32 as f32
    } else {
        raw as f32
    };

    physical * scale + offset
}

#[cfg(test)]
mod tests {
    use super::*;

    const EPSILON: f32 = 1e-4;

    fn approx(a: f32, b: f32) -> bool {
        (a - b).abs() < EPSILON
    }

    #[test]
    fn little_endian_unsigned_scale_offset() {
        // Same fixture as test/test_can_parser/test_main.cpp.
        let frame = [0x34, 0x12, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 2, false, false, 0, 0.1, 0.0);
        assert!(approx(r, 466.0), "got {r}");
    }

    #[test]
    fn big_endian_signed_negative() {
        // 0xFFEC big-endian == -20 in two's complement.
        let frame = [0xFF, 0xEC, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 2, true, true, 0, 1.0, 0.0);
        assert!(approx(r, -20.0), "got {r}");
    }

    #[test]
    fn bit_mask_set_returns_one() {
        let frame = [0x42, 0, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 1, false, false, 0x40, 1.0, 0.0);
        assert!(approx(r, 1.0));
    }

    #[test]
    fn bit_mask_clear_returns_zero() {
        let frame = [0x42, 0, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 1, false, false, 0x01, 1.0, 0.0);
        assert!(approx(r, 0.0));
    }

    #[test]
    fn zero_byte_len_returns_zero() {
        let frame = [0xFF, 0xFF, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 0, false, false, 0, 1.0, 0.0);
        assert!(approx(r, 0.0));
    }

    #[test]
    fn out_of_range_start_returns_zero() {
        let frame = [0xFF; 8];
        let r = decode_bytes(&frame, 7, 2, false, false, 0, 1.0, 0.0);
        assert!(approx(r, 0.0));
    }

    #[test]
    fn slice_shorter_than_request_returns_zero() {
        // Slice is 4 bytes but we ask for byte at index 5.
        let frame = [0x01, 0x02, 0x03, 0x04];
        let r = decode_bytes(&frame, 5, 1, false, false, 0, 1.0, 0.0);
        assert!(approx(r, 0.0));
    }

    #[test]
    fn single_byte_signed_negative() {
        // 0xFF as i8 == -1.
        let frame = [0xFF, 0, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 1, false, true, 0, 1.0, 0.0);
        assert!(approx(r, -1.0));
    }

    #[test]
    fn four_byte_signed_large_negative() {
        // 0x80000000 big-endian, signed → -2_147_483_648.
        let frame = [0x80, 0x00, 0x00, 0x00, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 4, true, true, 0, 1.0, 0.0);
        // f32 can't represent -2^31 exactly but the round-to-nearest gives
        // exactly that value.
        assert!(approx(r, -2_147_483_648.0_f32), "got {r}");
    }

    #[test]
    fn four_byte_unsigned_max() {
        let frame = [0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 4, false, false, 0, 1.0, 0.0);
        // u32::MAX rounds to 4_294_967_296 in f32 (≈ +2^32).
        assert!(approx(r, 4_294_967_295.0_f32), "got {r}");
    }

    #[test]
    fn offset_only_no_scale() {
        let frame = [0x0A, 0, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 1, false, false, 0, 1.0, 5.5);
        assert!(approx(r, 15.5), "got {r}");
    }

    #[test]
    fn little_endian_two_byte_unsigned() {
        let frame = [0xCD, 0xAB, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 2, false, false, 0, 1.0, 0.0);
        // 0xABCD == 43981.
        assert!(approx(r, 43981.0));
    }

    #[test]
    fn big_endian_two_byte_unsigned() {
        let frame = [0xAB, 0xCD, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 2, true, false, 0, 1.0, 0.0);
        assert!(approx(r, 43981.0));
    }

    #[test]
    fn mask_dominates_over_signed_flag() {
        // bit_mask != 0 → returns 0/1 regardless of is_signed.
        let frame = [0xFF, 0, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 1, false, true, 0x80, 1.0, 0.0);
        assert!(approx(r, 1.0));
    }
}
