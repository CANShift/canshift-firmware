// Rust port of error_store.cpp ring-buffer math (#1177 R-5).
// C++ wrapper holds the portMUX lock across every call — Rust assumes exclusive access.
#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

// Required for no_std staticlib — reaching here means invariant break.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

pub const CODE_LEN: usize = 12;
pub const MESSAGE_LEN: usize = 52;

// repr(C) is load-bearing — C++ accesses by pointer at the same offsets.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct FwError {
    pub source: u8,
    pub code: [u8; CODE_LEN],
    pub message: [u8; MESSAGE_LEN],
}

// Updates in-place on dup (source, code); otherwise inserts at next free slot,
// evicting oldest when full. Always bumps version.
pub fn push(
    ring: &mut [FwError],
    head: &mut u8,
    count: &mut u8,
    version: &mut u32,
    source: u8,
    code: &[u8],
    message: &[u8],
) {
    let ring_size = ring.len() as u8;
    if ring_size == 0 {
        return;
    }

    // strncmp semantics — match up to first NUL or CODE_LEN.
    for i in 0..*count {
        let idx = (head.wrapping_add(i)) % ring_size;
        let entry = &mut ring[idx as usize];
        if entry.source == source && code_matches(&entry.code, code) {
            copy_into_fixed(&mut entry.message, message);
            *version = version.wrapping_add(1);
            return;
        }
    }

    let slot = if *count < ring_size {
        let s = (head.wrapping_add(*count)) % ring_size;
        *count += 1;
        s
    } else {
        let s = *head;
        *head = (head.wrapping_add(1)) % ring_size;
        s
    };

    let entry = &mut ring[slot as usize];
    entry.source = source;
    copy_into_fixed(&mut entry.code, code);
    copy_into_fixed(&mut entry.message, message);
    *version = version.wrapping_add(1);
}

// Newest-first copy into `out`; returns number written. Doesn't mutate state.
#[must_use]
pub fn get_all(ring: &[FwError], head: u8, count: u8, out: &mut [FwError]) -> u8 {
    let ring_size = ring.len() as u8;
    if ring_size == 0 || count == 0 || out.is_empty() {
        return 0;
    }
    let n = count.min(out.len() as u8);
    for i in 0..n {
        let raw = (head as i32) + (count as i32) - 1 - (i as i32);
        let idx = ((raw % (ring_size as i32) + (ring_size as i32)) % (ring_size as i32)) as u8;
        out[i as usize] = ring[idx as usize];
    }
    n
}

// Out-of-range row is a no-op (and doesn't bump version).
pub fn dismiss_at(
    ring: &mut [FwError],
    head: &mut u8,
    count: &mut u8,
    version: &mut u32,
    row: u8,
) {
    let ring_size = ring.len() as u8;
    if ring_size == 0 || row >= *count {
        return;
    }

    // pos == 0 → oldest (advance head); pos == count-1 → newest (no copy).
    let pos = (*count - 1).wrapping_sub(row);

    if pos == 0 {
        *head = (head.wrapping_add(1)) % ring_size;
    } else if pos < count.saturating_sub(1) {
        let mut i = pos;
        while i + 1 < *count {
            let dst = (head.wrapping_add(i)) % ring_size;
            let src = (head.wrapping_add(i + 1)) % ring_size;
            ring[dst as usize] = ring[src as usize];
            i += 1;
        }
    }

    *count -= 1;
    *version = version.wrapping_add(1);
}

// strncmp-equivalent against the fixed-width `code` field.
fn code_matches(stored: &[u8], incoming: &[u8]) -> bool {
    for i in 0..stored.len() {
        let a = stored[i];
        let b = if i < incoming.len() { incoming[i] } else { 0 };
        if a != b {
            return false;
        }
        if a == 0 {
            return true;
        }
    }
    incoming.len() <= stored.len()
        || incoming[stored.len()] == 0
        || incoming.iter().take(stored.len()).all(|&b| b != 0) && incoming.len() == stored.len()
}

// strncpy semantics — truncate at dst.len()-1, zero-fill the tail.
fn copy_into_fixed(dst: &mut [u8], src: &[u8]) {
    let max = dst.len() - 1;
    let src_effective = source_until_nul(src, max);
    let copy_len = src_effective.len().min(max);
    dst[..copy_len].copy_from_slice(&src_effective[..copy_len]);
    for byte in &mut dst[copy_len..] {
        *byte = 0;
    }
}

fn source_until_nul(src: &[u8], cap: usize) -> &[u8] {
    let limit = src.len().min(cap);
    let end = src
        .iter()
        .take(limit)
        .position(|&b| b == 0)
        .unwrap_or(limit);
    &src[..end]
}

#[cfg(test)]
mod tests {
    use super::*;

    const RING: usize = 6;

    fn blank_ring() -> [FwError; RING] {
        [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; RING]
    }

    fn push_one(
        ring: &mut [FwError; RING],
        head: &mut u8,
        count: &mut u8,
        version: &mut u32,
        source: u8,
        code: &str,
        message: &str,
    ) {
        push(ring, head, count, version, source, code.as_bytes(), message.as_bytes());
    }

    fn code_of(e: &FwError) -> &str {
        let end = e.code.iter().position(|&b| b == 0).unwrap_or(e.code.len());
        core::str::from_utf8(&e.code[..end]).unwrap()
    }

    fn message_of(e: &FwError) -> &str {
        let end = e.message.iter().position(|&b| b == 0).unwrap_or(e.message.len());
        core::str::from_utf8(&e.message[..end]).unwrap()
    }

    #[test]
    fn fw_error_size_matches_cxx() {
        assert_eq!(core::mem::size_of::<FwError>(), 1 + 12 + 52);
    }

    #[test]
    fn push_to_empty_inserts_at_head() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "CODE", "first");
        assert_eq!(count, 1);
        assert_eq!(head, 0);
        assert_eq!(version, 1);
        assert_eq!(code_of(&ring[0]), "CODE");
        assert_eq!(message_of(&ring[0]), "first");
    }

    #[test]
    fn push_duplicate_updates_message_no_eviction() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "DUP", "old");
        push_one(&mut ring, &mut head, &mut count, &mut version, 2, "OTHER", "x");
        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "DUP", "new");

        assert_eq!(count, 2);
        assert_eq!(version, 3);
        assert_eq!(message_of(&ring[0]), "new");
        assert_eq!(message_of(&ring[1]), "x");
    }

    #[test]
    fn push_over_capacity_overwrites_oldest_advances_head() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        for i in 0..6 {
            let code = format!("C{i}");
            let msg = format!("m{i}");
            push_one(&mut ring, &mut head, &mut count, &mut version, 1, &code, &msg);
        }
        assert_eq!(count, 6);
        assert_eq!(head, 0);

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "C6", "m6");
        assert_eq!(count, 6);
        assert_eq!(head, 1);
        assert_eq!(code_of(&ring[0]), "C6");
    }

    #[test]
    fn push_truncates_too_long_message() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        let long_msg = "x".repeat(60);
        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "C", &long_msg);
        assert_eq!(message_of(&ring[0]).len(), MESSAGE_LEN - 1);
        assert_eq!(ring[0].message[MESSAGE_LEN - 1], 0);
    }

    #[test]
    fn get_all_returns_newest_first() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "A", "first");
        push_one(&mut ring, &mut head, &mut count, &mut version, 2, "B", "second");
        push_one(&mut ring, &mut head, &mut count, &mut version, 3, "C", "third");

        let mut out = [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; 4];
        let n = get_all(&ring, head, count, &mut out);
        assert_eq!(n, 3);
        assert_eq!(code_of(&out[0]), "C");
        assert_eq!(code_of(&out[1]), "B");
        assert_eq!(code_of(&out[2]), "A");
    }

    #[test]
    fn get_all_caps_at_out_len() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        for i in 0..6 {
            let code = format!("C{i}");
            push_one(&mut ring, &mut head, &mut count, &mut version, 1, &code, "");
        }

        let mut out = [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; 2];
        let n = get_all(&ring, head, count, &mut out);
        assert_eq!(n, 2);
        assert_eq!(code_of(&out[0]), "C5");
        assert_eq!(code_of(&out[1]), "C4");
    }

    #[test]
    fn get_all_handles_post_wrap_state() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        for i in 0..7 {
            let code = format!("C{i}");
            push_one(&mut ring, &mut head, &mut count, &mut version, 1, &code, "");
        }
        assert_eq!(head, 1);
        assert_eq!(count, 6);

        let mut out = [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; 6];
        let n = get_all(&ring, head, count, &mut out);
        assert_eq!(n, 6);
        for (i, expected) in ["C6", "C5", "C4", "C3", "C2", "C1"].iter().enumerate() {
            assert_eq!(code_of(&out[i]), *expected, "row {i}");
        }
    }

    #[test]
    fn dismiss_at_newest_matches_dismiss_latest() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "A", "");
        push_one(&mut ring, &mut head, &mut count, &mut version, 2, "B", "");
        let v_before = version;

        dismiss_at(&mut ring, &mut head, &mut count, &mut version, 0);
        assert_eq!(count, 1);
        assert_eq!(version, v_before + 1);
        let mut out = [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; 1];
        get_all(&ring, head, count, &mut out);
        assert_eq!(code_of(&out[0]), "A");
    }

    #[test]
    fn dismiss_at_oldest_advances_head() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "A", "");
        push_one(&mut ring, &mut head, &mut count, &mut version, 2, "B", "");
        push_one(&mut ring, &mut head, &mut count, &mut version, 3, "C", "");
        dismiss_at(&mut ring, &mut head, &mut count, &mut version, 2);
        assert_eq!(count, 2);
        assert_eq!(head, 1);
    }

    #[test]
    fn dismiss_at_middle_collapses_gap() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "A", "");
        push_one(&mut ring, &mut head, &mut count, &mut version, 2, "B", "");
        push_one(&mut ring, &mut head, &mut count, &mut version, 3, "C", "");
        dismiss_at(&mut ring, &mut head, &mut count, &mut version, 1);
        assert_eq!(count, 2);
        let mut out = [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; 2];
        get_all(&ring, head, count, &mut out);
        assert_eq!(code_of(&out[0]), "C");
        assert_eq!(code_of(&out[1]), "A");
    }

    #[test]
    fn dismiss_at_out_of_range_is_noop() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "A", "");
        let v_before = version;
        dismiss_at(&mut ring, &mut head, &mut count, &mut version, 99);
        assert_eq!(count, 1);
        assert_eq!(version, v_before);
    }

    #[test]
    fn dismiss_at_empty_is_noop() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        dismiss_at(&mut ring, &mut head, &mut count, &mut version, 0);
        assert_eq!(count, 0);
        assert_eq!(version, 0);
    }
}
