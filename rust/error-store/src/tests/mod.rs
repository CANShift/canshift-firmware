use super::*;

mod dismiss;
mod get_all;

pub(super) const RING: usize = 6;

pub(super) fn blank_ring() -> [FwError; RING] {
    [FwError {
        source: 0,
        code: [0; CODE_LEN],
        message: [0; MESSAGE_LEN],
    }; RING]
}

pub(super) struct Store {
    pub(super) ring: [FwError; RING],
    pub(super) head: u8,
    pub(super) count: u8,
    pub(super) version: u32,
}

impl Store {
    pub(super) fn new() -> Self {
        Store {
            ring: blank_ring(),
            head: 0,
            count: 0,
            version: 0,
        }
    }

    pub(super) fn push(&mut self, source: u8, code: &str, message: &str) {
        push(
            &mut self.ring,
            &mut self.head,
            &mut self.count,
            &mut self.version,
            source,
            code.as_bytes(),
            message.as_bytes(),
        );
    }
}

pub(super) fn code_of(e: &FwError) -> &str {
    let end = e.code.iter().position(|&b| b == 0).unwrap_or(e.code.len());
    core::str::from_utf8(&e.code[..end]).unwrap()
}

pub(super) fn message_of(e: &FwError) -> &str {
    let end = e
        .message
        .iter()
        .position(|&b| b == 0)
        .unwrap_or(e.message.len());
    core::str::from_utf8(&e.message[..end]).unwrap()
}

#[test]
fn fw_error_size_matches_cxx() {
    assert_eq!(core::mem::size_of::<FwError>(), 1 + 12 + 52);
}

#[test]
fn push_to_empty_inserts_at_head() {
    let mut s = Store::new();

    s.push(1, "CODE", "first");
    assert_eq!(s.count, 1);
    assert_eq!(s.head, 0);
    assert_eq!(s.version, 1);
    assert_eq!(code_of(&s.ring[0]), "CODE");
    assert_eq!(message_of(&s.ring[0]), "first");
}

#[test]
fn push_duplicate_updates_message_no_eviction() {
    let mut s = Store::new();

    s.push(1, "DUP", "old");
    s.push(2, "OTHER", "x");
    s.push(1, "DUP", "new");

    assert_eq!(s.count, 2);
    assert_eq!(s.version, 3);
    assert_eq!(message_of(&s.ring[0]), "new");
    assert_eq!(message_of(&s.ring[1]), "x");
}

#[test]
fn push_over_capacity_overwrites_oldest_advances_head() {
    let mut s = Store::new();

    for i in 0..6 {
        let code = format!("C{i}");
        let msg = format!("m{i}");
        s.push(1, &code, &msg);
    }
    assert_eq!(s.count, 6);
    assert_eq!(s.head, 0);

    s.push(1, "C6", "m6");
    assert_eq!(s.count, 6);
    assert_eq!(s.head, 1);
    assert_eq!(code_of(&s.ring[0]), "C6");
}

#[test]
fn push_truncates_too_long_message() {
    let mut s = Store::new();

    let long_msg = "x".repeat(60);
    s.push(1, "C", &long_msg);
    assert_eq!(message_of(&s.ring[0]).len(), MESSAGE_LEN - 1);
    assert_eq!(s.ring[0].message[MESSAGE_LEN - 1], 0);
}

#[test]
fn push_long_code_dedupes_on_truncated_key() {
    let mut s = Store::new();

    for (code, message) in [("VER_MISMATCH", "first"), ("VER_MISMATCH", "second")] {
        s.push(1, code, message);
    }

    assert_eq!(s.count, 1);
    assert_eq!(code_of(&s.ring[0]), "VER_MISMATC");
    assert_eq!(message_of(&s.ring[0]), "second");
}
