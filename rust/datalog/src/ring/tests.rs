use super::*;

const CAPACITY: u32 = 16;

fn ring_over(storage: &mut [u8]) -> Ring {
    unsafe { Ring::new(storage.as_mut_ptr(), storage.len() as u32) }.expect("valid ring")
}

#[test]
fn a_capacity_that_is_not_a_power_of_two_is_refused() {
    let mut storage = [0u8; 12];
    assert!(unsafe { Ring::new(storage.as_mut_ptr(), 12) }.is_none());
    assert!(unsafe { Ring::new(core::ptr::null_mut(), CAPACITY) }.is_none());
}

#[test]
fn bytes_come_out_in_the_order_they_went_in() {
    let mut storage = [0u8; CAPACITY as usize];
    let ring = ring_over(&mut storage);
    assert!(ring.push(b"abc"));
    assert!(ring.push(b"de"));
    let mut out = [0u8; 8];
    assert_eq!(ring.pop_into(&mut out), 5);
    assert_eq!(&out[..5], b"abcde");
    assert_eq!(ring.used(), 0);
}

#[test]
fn a_record_that_does_not_fit_is_dropped_whole_and_counted() {
    let mut storage = [0u8; CAPACITY as usize];
    let ring = ring_over(&mut storage);
    assert!(ring.push(&[1; 12]));
    assert!(!ring.push(&[2; 5]));
    assert_eq!(ring.used(), 12);
    assert_eq!(ring.dropped(), 1);
    assert!(ring.push(&[3; 4]));
    assert_eq!(ring.used(), CAPACITY);
}

#[test]
fn a_record_wraps_around_the_end_of_the_storage() {
    let mut storage = [0u8; CAPACITY as usize];
    let ring = ring_over(&mut storage);
    let mut out = [0u8; CAPACITY as usize];
    assert!(ring.push(&[0; 12]));
    assert_eq!(ring.pop_into(&mut out), 12);
    assert!(ring.push(b"0123456789"));
    assert_eq!(ring.pop_into(&mut out[..4]), 4);
    assert_eq!(&out[..4], b"0123");
    assert_eq!(ring.pop_into(&mut out), 6);
    assert_eq!(&out[..6], b"456789");
}

#[test]
fn the_positions_survive_the_u32_wrap() {
    let mut storage = [0u8; CAPACITY as usize];
    let ring = ring_over(&mut storage);
    ring.head.store(u32::MAX - 2, Ordering::Relaxed);
    ring.tail.store(u32::MAX - 2, Ordering::Relaxed);
    assert!(ring.push(b"wrap!"));
    assert_eq!(ring.used(), 5);
    let mut out = [0u8; 8];
    assert_eq!(ring.pop_into(&mut out), 5);
    assert_eq!(&out[..5], b"wrap!");
}

#[test]
fn an_empty_ring_pops_nothing() {
    let mut storage = [0u8; CAPACITY as usize];
    let ring = ring_over(&mut storage);
    let mut out = [0u8; 4];
    assert_eq!(ring.pop_into(&mut out), 0);
}
