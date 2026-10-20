use super::*;
use crate::crc::crc16;

const RPM: usize = 0;
const THROTTLE: usize = 1;
const GEAR: usize = 12;

fn canonical_values() -> [f32; DATALOG_SIGNAL_SLOTS] {
    let mut values = [0.0; DATALOG_SIGNAL_SLOTS];
    values[RPM] = 3500.0;
    values[THROTTLE] = 45.5;
    values[GEAR] = -1.0;
    values
}

fn canonical_mask() -> u64 {
    (1 << RPM) | (1 << THROTTLE) | (1 << GEAR)
}

fn frame_crc_holds(frame: &[u8]) -> bool {
    let (body, crc) = frame.split_at(frame.len() - DATALOG_FRAME_CRC_BYTES);
    crc16(body).to_le_bytes() == crc
}

#[test]
fn preamble_is_magic_then_version() {
    let mut out = [0u8; 8];
    assert_eq!(encode_preamble(&mut out), DATALOG_PREAMBLE_BYTES);
    assert_eq!(DATALOG_PREAMBLE_BYTES, DATALOG_MAGIC.len() + 1);
    assert_eq!(&out[..5], b"CSLG\x01");
}

#[test]
fn sample_matches_the_canonical_vector_shared_with_core() {
    let mut out = [0u8; DATALOG_MAX_SAMPLE_BYTES];
    let len = encode_sample(&mut out, 1000, canonical_mask(), &canonical_values());
    let expected: [u8; 29] = [
        0x02, 0x18, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x03, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xc0, 0x5a, 0x45, 0x00, 0x00, 0x36, 0x42, 0x00, 0x00, 0x80, 0xbf, 0xd8, 0xf1,
    ];
    assert_eq!(&out[..len], &expected);
}

#[test]
fn lap_and_end_match_the_canonical_vectors_shared_with_core() {
    let mut out = [0u8; 32];
    let lap = encode_lap(&mut out, 61_000, 1, 60_500);
    assert_eq!(
        &out[..lap],
        &[
            0x03, 0x0a, 0x00, 0x48, 0xee, 0x00, 0x00, 0x01, 0x00, 0x54, 0xec, 0x00, 0x00, 0x60,
            0xd9
        ]
    );
    let end = encode_end(&mut out, 62_000, 3);
    assert_eq!(
        &out[..end],
        &[0x04, 0x08, 0x00, 0x30, 0xf2, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x78, 0x5d]
    );
}

#[test]
fn a_non_finite_value_leaves_the_mask() {
    let mut values = canonical_values();
    values[THROTTLE] = f32::NAN;
    let mut out = [0u8; DATALOG_MAX_SAMPLE_BYTES];
    let len = encode_sample(&mut out, 0, canonical_mask(), &values);
    assert_eq!(out[7], 0x01);
    assert_eq!(out[8], 0x10);
    assert_eq!(
        len,
        DATALOG_FRAME_PREFIX_BYTES + 12 + 2 * 4 + DATALOG_FRAME_CRC_BYTES
    );
}

#[test]
fn a_full_sample_fits_the_largest_frame() {
    let mut out = [0u8; DATALOG_MAX_SAMPLE_BYTES];
    let values = [1.0; DATALOG_SIGNAL_SLOTS];
    assert_eq!(
        encode_sample(&mut out, 0, u64::MAX, &values),
        DATALOG_MAX_SAMPLE_BYTES
    );
}

#[test]
fn a_short_buffer_writes_nothing() {
    let mut out = [0u8; DATALOG_MAX_SAMPLE_BYTES - 1];
    let values = [1.0; DATALOG_SIGNAL_SLOTS];
    assert_eq!(encode_sample(&mut out, 0, u64::MAX, &values), 0);
    let mut tiny = [0u8; 4];
    assert_eq!(encode_lap(&mut tiny, 0, 0, 0), 0);
    assert_eq!(encode_preamble(&mut tiny), 0);
}

#[test]
fn header_carries_the_period_the_session_and_the_catalog_names() {
    let mut out = [0u8; DATALOG_MAX_HEADER_BYTES];
    let len = encode_header(&mut out, 50, 7);
    let catalog = signal_map::catalog();
    assert_eq!(out[0], DATALOG_RECORD_HEADER);
    assert_eq!(usize::from(u16::from_le_bytes([out[1], out[2]])), len - 5);
    assert_eq!(&out[3..7], &[50, 0, 7, 0]);
    assert_eq!(usize::from(out[7]), catalog.len());
    assert_eq!(&out[8..13], &[0, 3, b'r', b'p', b'm']);
    assert!(frame_crc_holds(&out[..len]));
}

#[test]
fn the_buffer_bounds_cover_the_largest_record_of_each_kind() {
    let prefix_and_crc = DATALOG_FRAME_PREFIX_BYTES + DATALOG_FRAME_CRC_BYTES;
    let largest_name_entry = 2 + signal_map::MAX_NAME_LEN;
    assert_eq!(
        DATALOG_MAX_SAMPLE_BYTES,
        prefix_and_crc + 4 + 8 + 4 * DATALOG_SIGNAL_SLOTS
    );
    assert_eq!(
        DATALOG_MAX_HEADER_BYTES,
        prefix_and_crc + 5 + largest_name_entry * DATALOG_SIGNAL_SLOTS
    );
    assert_eq!(DATALOG_MAX_MARKER_BYTES, prefix_and_crc + 4 + 2 + 4);
    let mut out = [0u8; DATALOG_MAX_HEADER_BYTES];
    assert!(encode_header(&mut out, 50, 1) > 0);
}
