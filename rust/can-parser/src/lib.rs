#![cfg_attr(not(any(test, feature = "std")), no_std)]

pub mod bus_recovery;
mod decimal;
pub mod expr;
pub mod expr_compile;
pub mod expr_ffi;
pub mod expr_run;

#[cfg(feature = "ffi")]
pub mod ffi;

pub const CAN_FRAME_MAX_BYTES: usize = 8;

#[must_use]
#[allow(clippy::too_many_arguments)]
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
    let start = start_byte as usize;
    let len = byte_len as usize;
    if start + len > CAN_FRAME_MAX_BYTES || start + len > data.len() {
        return 0.0;
    }

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

    if bit_mask != 0 {
        return if raw & bit_mask as u32 != 0 {
            1.0
        } else {
            0.0
        };
    }

    let physical: f32 = if is_signed {
        let bits = (len as u32) * 8;
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
        let frame = [0x34, 0x12, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 2, false, false, 0, 0.1, 0.0);
        assert!(approx(r, 466.0), "got {r}");
    }

    #[test]
    fn big_endian_signed_negative() {
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
        let frame = [0x01, 0x02, 0x03, 0x04];
        let r = decode_bytes(&frame, 5, 1, false, false, 0, 1.0, 0.0);
        assert!(approx(r, 0.0));
    }

    #[test]
    fn single_byte_signed_negative() {
        let frame = [0xFF, 0, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 1, false, true, 0, 1.0, 0.0);
        assert!(approx(r, -1.0));
    }

    #[test]
    fn four_byte_signed_large_negative() {
        let frame = [0x80, 0x00, 0x00, 0x00, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 4, true, true, 0, 1.0, 0.0);
        assert!(approx(r, -2_147_483_648.0_f32), "got {r}");
    }

    #[test]
    fn four_byte_unsigned_max() {
        let frame = [0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 4, false, false, 0, 1.0, 0.0);
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
        let frame = [0xFF, 0, 0, 0, 0, 0, 0, 0];
        let r = decode_bytes(&frame, 0, 1, false, true, 0x80, 1.0, 0.0);
        assert!(approx(r, 1.0));
    }
}
