use crate::expr::{eval_ffi, lex_to_ffi, EvalContext, FfiTok};
use crate::{decode_bytes, CAN_FRAME_MAX_BYTES};
use core::slice;

/// # Safety: `data` readable for `data_len` bytes (capped at CAN_FRAME_MAX_BYTES; null → 0).
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
    data_len: usize,
) -> f32 {
    if data.is_null() {
        return 0.0;
    }
    let len = data_len.min(CAN_FRAME_MAX_BYTES);
    let slice = unsafe { slice::from_raw_parts(data, len) };
    decode_bytes(
        slice, start_byte, byte_len, big_endian, is_signed, bit_mask, scale, offset,
    )
}

/// # Safety: `expr` readable for `expr_len` bytes; `out` writable for `out_cap` `FfiTok`.
/// Returns the token count on success, or -1 on lex error / insufficient capacity.
#[no_mangle]
pub unsafe extern "C" fn lex_expr_rs(
    expr: *const u8,
    expr_len: usize,
    out: *mut FfiTok,
    out_cap: usize,
) -> i32 {
    if expr.is_null() || out.is_null() || expr_len == 0 {
        return 0;
    }
    let expr_slice = unsafe { slice::from_raw_parts(expr, expr_len) };
    let out_slice = unsafe { slice::from_raw_parts_mut(out, out_cap) };
    match lex_to_ffi(expr_slice, out_slice) {
        Some(n) => n as i32,
        None => -1,
    }
}

/// # Safety: `data` readable for `data_len` bytes (capped at CAN_FRAME_MAX_BYTES);
/// `tokens` readable for `n_tokens` `FfiTok` produced by `lex_expr_rs`.
#[no_mangle]
pub unsafe extern "C" fn eval_tokens_rs(
    data: *const u8,
    start_byte: u8,
    byte_len: u8,
    big_endian: bool,
    is_signed: bool,
    bit_mask: u8,
    scale: f32,
    offset: f32,
    data_len: usize,
    tokens: *const FfiTok,
    n_tokens: usize,
) -> f32 {
    if data.is_null() {
        return 0.0;
    }
    let len = data_len.min(CAN_FRAME_MAX_BYTES);
    let frame = unsafe { slice::from_raw_parts(data, len) };
    let v = decode_bytes(
        frame, start_byte, byte_len, big_endian, is_signed, bit_mask, scale, offset,
    );
    if tokens.is_null() || n_tokens == 0 {
        return 0.0;
    }
    let toks = unsafe { slice::from_raw_parts(tokens, n_tokens) };
    let ctx = EvalContext { v, bytes: frame };
    eval_ffi(toks, &ctx)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_matches_native_for_known_fixture() {
        let frame = [0x34, 0x12, 0, 0, 0, 0, 0, 0];
        let r = unsafe {
            decode_bytes_rs(frame.as_ptr(), 0, 2, false, false, 0, 0.1, 0.0, frame.len())
        };
        assert!((r - 466.0).abs() < 1e-4, "got {r}");
    }

    #[test]
    fn ffi_null_data_returns_zero() {
        let r = unsafe { decode_bytes_rs(core::ptr::null(), 0, 1, false, false, 0, 1.0, 0.0, 8) };
        assert_eq!(r, 0.0);
    }

    #[test]
    fn ffi_short_data_len_rejects_out_of_range_field() {
        let frame = [0x34, 0x12, 0, 0, 0, 0, 0, 0];
        let r = unsafe { decode_bytes_rs(frame.as_ptr(), 0, 2, false, false, 0, 0.1, 0.0, 1) };
        assert_eq!(r, 0.0);
    }
}
