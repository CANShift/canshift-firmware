use super::*;

fn as_str(buf: &[u8]) -> &str {
    let end = find_nul(buf);
    core::str::from_utf8(&buf[..end]).unwrap()
}

#[test]
fn fixed_basic_two_decimals() {
    let mut buf = [0u8; 32];
    let n = format_fixed(&mut buf, 8.14159, 2);
    assert_eq!(n, 4);
    assert_eq!(as_str(&buf), "8.14");
}

#[test]
fn fixed_zero_decimals_rounds() {
    let mut buf = [0u8; 32];
    let n = format_fixed(&mut buf, 3.7, 0);
    assert_eq!(n, 1);
    assert_eq!(as_str(&buf), "4");
}

#[test]
fn fixed_negative_value() {
    let mut buf = [0u8; 32];
    assert_eq!(format_fixed(&mut buf, -12.5, 1), 5);
    assert_eq!(as_str(&buf), "-12.5");
}

#[test]
fn fixed_handles_zero() {
    let mut buf = [0u8; 32];
    assert_eq!(format_fixed(&mut buf, 0.0, 3), 5);
    assert_eq!(as_str(&buf), "0.000");
}

#[test]
fn fixed_nan() {
    let mut buf = [0u8; 32];
    let n = format_fixed(&mut buf, f32::NAN, 2);
    assert_eq!(n, 3);
    assert_eq!(as_str(&buf), "nan");
}

#[test]
fn fixed_positive_infinity() {
    let mut buf = [0u8; 32];
    assert_eq!(format_fixed(&mut buf, f32::INFINITY, 2), 3);
    assert_eq!(as_str(&buf), "inf");
}

#[test]
fn fixed_negative_infinity() {
    let mut buf = [0u8; 32];
    assert_eq!(format_fixed(&mut buf, f32::NEG_INFINITY, 2), 4);
    assert_eq!(as_str(&buf), "-inf");
}

#[test]
fn fixed_clamps_decimals_high() {
    let mut buf = [0u8; 32];
    assert_eq!(format_fixed(&mut buf, 1.0, 99), 11);
    assert_eq!(as_str(&buf), "1.000000000");
}

#[test]
fn fixed_clamps_decimals_negative() {
    let mut buf = [0u8; 32];
    assert_eq!(format_fixed(&mut buf, 1.5, -1), 1);
    assert_eq!(as_str(&buf), "2");
}

#[test]
fn fixed_truncates_to_buffer_with_nul() {
    let mut buf = [0u8; 4];
    let n = format_fixed(&mut buf, 123.456, 2);
    assert_eq!(n, 6);
    assert_eq!(as_str(&buf), "123");
    assert_eq!(buf[3], 0);
}

#[test]
fn fixed_empty_buffer_returns_zero() {
    let mut buf = [];
    assert_eq!(format_fixed(&mut buf, 1.0, 2), 0);
}

#[test]
fn spec_no_token_falls_back_to_one_decimal() {
    let mut buf = [0u8; 32];
    let n = format_from_spec(&mut buf, 12.34, b" V");
    assert_eq!(n, 4);
    assert_eq!(as_str(&buf), "12.3");
}

#[test]
fn spec_default_precision() {
    let mut buf = [0u8; 32];
    assert_eq!(format_from_spec(&mut buf, 5.4321, b"%fV"), 4);
    assert_eq!(as_str(&buf), "5.4V");
}

#[test]
fn spec_explicit_precision() {
    let mut buf = [0u8; 32];
    assert_eq!(format_from_spec(&mut buf, 5.4321, b"%.3fV"), 6);
    assert_eq!(as_str(&buf), "5.432V");
}

#[test]
fn spec_zero_precision() {
    let mut buf = [0u8; 32];
    assert_eq!(format_from_spec(&mut buf, 5.7, b"%.0f bar"), 5);
    assert_eq!(as_str(&buf), "6 bar");
}

#[test]
fn spec_prefix_and_suffix() {
    let mut buf = [0u8; 32];
    let n = format_from_spec(&mut buf, 8.14, b"value=%.2fkg!");
    assert_eq!(n, 13);
    assert_eq!(as_str(&buf), "value=8.14kg!");
}

#[test]
fn spec_double_percent_is_skip_not_unescape() {
    let mut buf = [0u8; 32];
    assert_eq!(format_from_spec(&mut buf, 9.8, b"%%V%.1f"), 6);
    assert_eq!(as_str(&buf), "%%V9.8");
}

#[test]
fn spec_unreasonable_precision_bails_and_keeps_scanning() {
    let mut buf = [0u8; 32];
    assert_eq!(format_from_spec(&mut buf, 4.5, b"%.123fV"), 3);
    assert_eq!(as_str(&buf), "4.5");
}

#[test]
fn spec_empty_string_is_one_decimal() {
    let mut buf = [0u8; 32];
    assert_eq!(format_from_spec(&mut buf, 1.2345, b""), 3);
    assert_eq!(as_str(&buf), "1.2");
}

#[test]
fn general_strips_trailing_zeros() {
    let mut buf = [0u8; 32];
    assert_eq!(format_general(&mut buf, 1.5, 4), 3);
    assert_eq!(as_str(&buf), "1.5");
}

#[test]
fn general_keeps_meaningful_decimals() {
    let mut buf = [0u8; 32];
    assert_eq!(format_general(&mut buf, 1.234, 4), 5);
    assert_eq!(as_str(&buf), "1.234");
}

#[test]
fn general_large_magnitude_no_decimals() {
    let mut buf = [0u8; 32];
    assert_eq!(format_general(&mut buf, 12345.0, 4), 5);
    assert_eq!(as_str(&buf), "12345");
}

#[test]
fn general_sub_unit_small() {
    let mut buf = [0u8; 32];
    assert_eq!(format_general(&mut buf, 0.123, 4), 5);
    assert_eq!(as_str(&buf), "0.123");
}

#[test]
fn general_negative() {
    let mut buf = [0u8; 32];
    assert_eq!(format_general(&mut buf, -1.5, 3), 4);
    assert_eq!(as_str(&buf), "-1.5");
}

#[test]
fn general_clamps_sig_digits_low() {
    let mut buf = [0u8; 32];
    assert_eq!(format_general(&mut buf, 9.876, 0), 2);
    assert_eq!(as_str(&buf), "10");
}

#[test]
fn general_nan_and_inf() {
    let mut buf = [0u8; 32];
    assert_eq!(format_general(&mut buf, f32::NAN, 3), 3);
    assert_eq!(as_str(&buf), "nan");

    assert_eq!(format_general(&mut buf, f32::INFINITY, 3), 3);
    assert_eq!(as_str(&buf), "inf");

    assert_eq!(format_general(&mut buf, f32::NEG_INFINITY, 3), 4);
    assert_eq!(as_str(&buf), "-inf");
}
