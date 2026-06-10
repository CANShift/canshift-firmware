use crate::{decode_bytes, CAN_FRAME_MAX_BYTES};
use core::slice;

/// # Safety: `data` readable for CAN_FRAME_MAX_BYTES bytes (or null → 0).
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
    let slice = unsafe { slice::from_raw_parts(data, CAN_FRAME_MAX_BYTES) };
    decode_bytes(slice, start_byte, byte_len, big_endian, is_signed, bit_mask, scale, offset)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_matches_native_for_known_fixture() {
        let frame = [0x34, 0x12, 0, 0, 0, 0, 0, 0];
        let r = unsafe { decode_bytes_rs(frame.as_ptr(), 0, 2, false, false, 0, 0.1, 0.0) };
        assert!((r - 466.0).abs() < 1e-4, "got {r}");
    }

    #[test]
    fn ffi_null_data_returns_zero() {
        let r = unsafe { decode_bytes_rs(core::ptr::null(), 0, 1, false, false, 0, 1.0, 0.0) };
        assert_eq!(r, 0.0);
    }
}
