use super::*;
use crate::filter::set_filter;
use crate::table::{offer, CAN_MONITOR_EXTENDED_ID};
use crate::vectors::{
    EXTENDED_EXACT_FILTER_VECTOR, FILTERED_EXTENDED_ID_VECTOR, TWO_STANDARD_IDS_VECTOR,
};

fn two_standard_ids() -> CanMonitor {
    let mut monitor = CanMonitor::default();
    let data = [0x0D, 0x7A, 0x00, 0x54, 0x1F, 0x40, 0x00, 0x00];
    offer(&mut monitor, 0x360, &data, 1000);
    offer(&mut monitor, 0x360, &data, 1010);
    offer(&mut monitor, 0x3E0, &[0x0B, 0x5E], 1040);
    monitor
}

#[test]
fn encodes_the_two_standard_ids_vector() {
    let mut monitor = two_standard_ids();
    let mut out = [0u8; CAN_MONITOR_MAX_FRAME_BYTES];
    let len = encode(&mut monitor, 1050, &mut out);
    assert_eq!(&out[..len], &TWO_STANDARD_IDS_VECTOR);
}

#[test]
fn encodes_the_filtered_extended_id_vector() {
    let mut monitor = CanMonitor::default();
    let id = 0x18FF_50E5 | CAN_MONITOR_EXTENDED_ID;
    assert!(set_filter(&mut monitor, &EXTENDED_EXACT_FILTER_VECTOR));
    assert!(offer(&mut monitor, id, &[], 70_000));
    let mut out = [0u8; CAN_MONITOR_MAX_FRAME_BYTES];
    let len = encode(&mut monitor, 70_000, &mut out);
    assert_eq!(&out[..len], &FILTERED_EXTENDED_ID_VECTOR);
}

#[test]
fn a_sent_entry_is_not_resent_until_a_new_frame_arrives() {
    let mut monitor = two_standard_ids();
    let mut out = [0u8; CAN_MONITOR_MAX_FRAME_BYTES];
    assert!(encode(&mut monitor, 1050, &mut out) > 0);
    assert_eq!(encode(&mut monitor, 1150, &mut out), 0);
    offer(&mut monitor, 0x3E0, &[1], 1160);
    assert_eq!(encode(&mut monitor, 1200, &mut out), 8 + 10);
    assert_eq!(out[1], 1);
}

#[test]
fn an_entry_that_does_not_fit_waits_for_the_next_frame() {
    let mut monitor = two_standard_ids();
    let mut out = [0u8; 8 + 17 + 5];
    assert_eq!(encode(&mut monitor, 1050, &mut out), 8 + 17);
    assert_eq!(encode(&mut monitor, 1060, &mut out), 8 + 11);
    assert_eq!(&out[8..12], &[0xe0, 0x03, 0x00, 0x00]);
}

#[test]
fn the_cursor_resumes_where_the_last_frame_stopped() {
    let mut monitor = CanMonitor::default();
    (0..20u32).for_each(|id| {
        offer(&mut monitor, id, &[0; 8], 0);
    });
    let mut out = [0u8; CAN_MONITOR_MAX_FRAME_BYTES];
    encode(&mut monitor, 0, &mut out);
    assert_eq!(out[1], 13);
    offer(&mut monitor, 0, &[0; 8], 5);
    encode(&mut monitor, 10, &mut out);
    assert_eq!(out[1], 8);
    assert_eq!(out[8], 13);
}

#[test]
fn the_payload_never_exceeds_the_notify_cap() {
    let mut monitor = CanMonitor::default();
    (0..64u32).for_each(|id| {
        offer(&mut monitor, id, &[0; 8], 0);
    });
    let mut out = [0u8; 512];
    assert!(encode(&mut monitor, 0, &mut out) <= CAN_MONITOR_MAX_FRAME_BYTES);
}

#[test]
fn the_age_saturates_and_the_overflow_flag_is_reported() {
    let mut monitor = CanMonitor::default();
    (0..=64u32).for_each(|id| {
        offer(&mut monitor, id, &[], 0);
    });
    let mut out = [0u8; CAN_MONITOR_MAX_FRAME_BYTES];
    encode(&mut monitor, 100_000, &mut out);
    assert_eq!(out[2], CAN_MONITOR_FLAG_OVERFLOW);
    assert_eq!(&out[15..17], &[0xff, 0xff]);
}

#[test]
fn an_empty_or_quiet_table_encodes_nothing() {
    let mut monitor = CanMonitor::default();
    let mut out = [0u8; CAN_MONITOR_MAX_FRAME_BYTES];
    assert_eq!(encode(&mut monitor, 0, &mut out), 0);
    assert_eq!(encode(&mut monitor, 0, &mut out[..4]), 0);
}
