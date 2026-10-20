use super::*;
use crate::filter::set_filter;
use crate::vectors::STANDARD_PREFIX_FILTER_VECTOR;

#[test]
fn a_repeated_id_keeps_one_entry_with_the_latest_data() {
    let mut monitor = CanMonitor::default();
    assert!(offer(&mut monitor, 0x360, &[1, 2, 3], 10));
    assert!(offer(&mut monitor, 0x360, &[9], 20));
    assert_eq!(monitor.used, 1);
    let entry = monitor.entries[0];
    assert_eq!(entry.count, 2);
    assert_eq!(entry.dlc, 1);
    assert_eq!(entry.data, [9, 0, 0, 0, 0, 0, 0, 0]);
    assert_eq!(entry.last_ms, 20);
    assert!(entry.dirty);
}

#[test]
fn a_payload_longer_than_a_classic_frame_is_clamped() {
    let mut monitor = CanMonitor::default();
    offer(&mut monitor, 0x100, &[7; 12], 0);
    assert_eq!(monitor.entries[0].dlc, 8);
}

#[test]
fn a_full_table_flags_overflow_and_keeps_counting_known_ids() {
    let mut monitor = CanMonitor::default();
    (0..CAN_MONITOR_ID_CAPACITY as u32).for_each(|id| {
        offer(&mut monitor, id, &[], 0);
    });
    assert!(!monitor.overflow);
    assert!(!offer(&mut monitor, 0x7FF, &[], 0));
    assert!(monitor.overflow);
    assert!(offer(&mut monitor, 0, &[], 0));
    assert_eq!(monitor.entries[0].count, 2);
}

#[test]
fn the_count_saturates_instead_of_wrapping() {
    let mut monitor = CanMonitor::default();
    (0..=u32::from(u16::MAX)).for_each(|_| {
        offer(&mut monitor, 0x200, &[], 0);
    });
    assert_eq!(monitor.entries[0].count, u16::MAX);
}

#[test]
fn a_filter_admits_only_matching_ids_and_clears_the_table() {
    let mut monitor = CanMonitor::default();
    offer(&mut monitor, 0x123, &[], 0);
    assert!(set_filter(&mut monitor, &STANDARD_PREFIX_FILTER_VECTOR));
    assert_eq!(monitor.used, 0);
    assert!(offer(&mut monitor, 0x36A, &[], 0));
    assert!(!offer(&mut monitor, 0x3E0, &[], 0));
    assert!(!offer(
        &mut monitor,
        0x360 | CAN_MONITOR_EXTENDED_ID,
        &[],
        0
    ));
    assert_eq!(monitor.used, 1);
}

#[test]
fn an_empty_filter_admits_every_id() {
    let mut monitor = CanMonitor::default();
    assert!(set_filter(&mut monitor, &[0x01, 0x00]));
    assert!(offer(
        &mut monitor,
        0x1FFF_FFFF | CAN_MONITOR_EXTENDED_ID,
        &[],
        0
    ));
}

#[test]
fn a_malformed_filter_is_refused_and_keeps_the_current_one() {
    let mut monitor = CanMonitor::default();
    let exact = [0x01, 0x01, 0x60, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF];
    assert!(set_filter(&mut monitor, &exact));
    assert!(!set_filter(&mut monitor, &[0x02, 0x00]));
    assert!(!set_filter(&mut monitor, &[0x01, 0x01, 0x00]));
    assert!(!set_filter(&mut monitor, &[0x01, 0x09]));
    assert!(!set_filter(&mut monitor, &[]));
    assert_eq!(monitor.filter_count, 1);
    assert!(!offer(&mut monitor, 0x361, &[], 0));
}

#[test]
fn reset_drops_the_table_and_the_filter() {
    let mut monitor = CanMonitor::default();
    set_filter(&mut monitor, &[0x01, 0x01, 0, 0, 0, 0, 0, 0, 0, 0]);
    offer(&mut monitor, 0x100, &[], 0);
    reset(&mut monitor);
    assert_eq!(monitor, CanMonitor::default());
}
