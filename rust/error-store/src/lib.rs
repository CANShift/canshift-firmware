// error-store — Rust port of the ring-buffer math in
// `canshift-firmware/src/diag/error_store.cpp`. Issue #1177 R-5.
//
// What stays C++:
//   - The portMUX spinlock (`s_mux`) — Rust can't manage it without a HAL
//     binding for FreeRTOS critical sections.
//   - The static buffer + state globals (`s_ring`, `s_head`, `s_count`,
//     `s_version`) — they live in `.bss`, exposed to Rust through raw
//     pointers under the lock.
//   - The four trivial accessors (`getCount`, `getVersion`, `dismissLatest`,
//     `clear`) — one-liner state mutations that don't carry enough logic to
//     justify an FFI boundary.
//
// What moves to Rust:
//   - `push` — duplicate-key in-place update, new-slot allocation, oldest-
//     overwrite when the ring is full. The strncpy + null-terminate-at-cap
//     dance is replaced with bounded slice copy.
//   - `getAll` — newest-first copy of up to `max_count` entries from the
//     ring into the caller's buffer.
//   - `dismissAt` — newest-first row dismissal with gap-collapse via shift
//     down. The arithmetic here is the most likely place for a one-off in a
//     future refactor — locking it down in slice operations removes the
//     class.
//
// The C++ wrapper (`error_store.cpp`) holds the lock for the entire RMW
// window — including the call into Rust — so the Rust functions are called
// with exclusive access guarantees by construction. The Rust impl does not
// know about the lock and does not try to acquire one.

#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

// Panic handler — required for `no_std + staticlib`. Halts forever. Same
// strategy as the other ports. None of the public functions panic on
// well-formed input; reaching this means an internal invariant break.
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

// Width of the `code` field in `FwError` — mirrors `error_store.h`. The
// Rust impl asserts this matches via `const _: () = assert!(…)` in the
// FFI shim against `core::mem::size_of`.
pub const CODE_LEN: usize = 12;

// Width of the `message` field in `FwError`. Same mirror rule as CODE_LEN.
pub const MESSAGE_LEN: usize = 52;

// Mirror of the C `FwError` struct. `#[repr(C)]` is load-bearing — the C++
// wrapper passes ring elements by pointer and the Rust slice access has to
// land on the same byte offsets the C++ writer used.
//
// Total size: 1 + 12 + 52 = 65 bytes, no padding (all 1-byte aligned).
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct FwError {
    pub source: u8,
    pub code: [u8; CODE_LEN],
    pub message: [u8; MESSAGE_LEN],
}

// ---------------------------------------------------------------------------
// Push — duplicate update / new-slot / oldest-overwrite
// ---------------------------------------------------------------------------

// Insert or update an error in the ring. If `(source, code)` already exists,
// updates the existing entry's `message` in place (NO eviction, NO ordering
// change). Otherwise inserts at the next free slot, evicting the oldest
// entry when the ring is full.
//
// `version` is incremented unconditionally so subscribers see the change.
// Mirrors `ErrorStore::push` byte-for-byte (the existing
// `test/test_error_store_wrap/` Unity suite is the parity gate).
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

    // Search for an existing entry with the same source+code. Match `code`
    // up to the first NUL or `CODE_LEN` boundary — same semantics as the
    // C++ `strncmp(..., sizeof(code))`.
    for i in 0..*count {
        let idx = (head.wrapping_add(i)) % ring_size;
        let entry = &mut ring[idx as usize];
        if entry.source == source && code_matches(&entry.code, code) {
            copy_into_fixed(&mut entry.message, message);
            *version = version.wrapping_add(1);
            return;
        }
    }

    // No match — pick a slot.
    let slot = if *count < ring_size {
        let s = (head.wrapping_add(*count)) % ring_size;
        *count += 1;
        s
    } else {
        // Ring full — overwrite oldest (head), advance head by one.
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

// ---------------------------------------------------------------------------
// Get all — newest-first copy out
// ---------------------------------------------------------------------------

// Copy up to `out.len()` errors from the ring into `out`, newest first.
// Returns the number of entries written. Does NOT mutate the ring or any
// state — the C++ wrapper's lock can be released immediately after.
#[must_use]
pub fn get_all(ring: &[FwError], head: u8, count: u8, out: &mut [FwError]) -> u8 {
    let ring_size = ring.len() as u8;
    if ring_size == 0 || count == 0 || out.is_empty() {
        return 0;
    }
    let n = count.min(out.len() as u8);
    for i in 0..n {
        // Newest-first traversal: index (head + count - 1 - i) mod ring_size.
        let raw = (head as i32) + (count as i32) - 1 - (i as i32);
        // Modulo is well-defined here because count <= ring_size and i < n.
        // Use u8 wrapping to avoid the i32 cast on the hot path post-LTO.
        let idx = ((raw % (ring_size as i32) + (ring_size as i32)) % (ring_size as i32)) as u8;
        out[i as usize] = ring[idx as usize];
    }
    n
}

// ---------------------------------------------------------------------------
// Dismiss at — drop one entry, collapse the gap
// ---------------------------------------------------------------------------

// Drop the entry at newest-first index `row`. Out-of-range rows are silent
// no-ops — callers don't need to pre-check `count`. Mirrors the C++ exactly:
//
//   - `row == 0` (newest) → just decrement count, no shift needed.
//   - `row == count - 1` (oldest) → advance head by one, no copy needed.
//   - `row` in the middle → shift entries one position toward head to close
//     the gap. The newest entry's prior slot is left untouched but
//     unreferenced (count-- means it's beyond the visible window).
//
// `version` advances on success only, never on a no-op — this matches the
// C++ and is locked down by the `test_dismissAt_versionAdvancesOnSuccess_…`
// Unity test.
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

    // The C++ converts newest-first `row` to oldest-first `pos`:
    //   pos = count - 1 - row
    // - pos == count - 1 → row == 0 → newest → no copy.
    // - pos == 0         → row == count - 1 → oldest → advance head.
    // - 0 < pos < count - 1 → middle → shift down.
    let pos = (*count - 1).wrapping_sub(row);

    if pos == 0 {
        // Dropping the oldest — advance head one step.
        *head = (head.wrapping_add(1)) % ring_size;
    } else if pos < count.saturating_sub(1) {
        // Middle — shift positions pos+1..count down by one, closing the gap.
        let mut i = pos;
        while i + 1 < *count {
            let dst = (head.wrapping_add(i)) % ring_size;
            let src = (head.wrapping_add(i + 1)) % ring_size;
            ring[dst as usize] = ring[src as usize];
            i += 1;
        }
    }
    // pos == count - 1 (newest) → fall through, no copy needed.

    *count -= 1;
    *version = version.wrapping_add(1);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// `strncmp`-equivalent for the fixed-size `code` field. Compares up to the
// first NUL on either side OR the field width — whichever comes first.
fn code_matches(stored: &[u8], incoming: &[u8]) -> bool {
    for i in 0..stored.len() {
        let a = stored[i];
        let b = if i < incoming.len() { incoming[i] } else { 0 };
        if a != b {
            return false;
        }
        if a == 0 {
            // Both sides are NUL at the same position — strings match up
            // to here, and C++ `strncmp` stops at the NUL.
            return true;
        }
    }
    // Walked the whole stored field without a NUL — match if the incoming
    // is also at-or-past its end (or NUL there).
    incoming.len() <= stored.len()
        || incoming[stored.len()] == 0
        || incoming.iter().take(stored.len()).all(|&b| b != 0) && incoming.len() == stored.len()
}

// Copy `src` into the fixed-size destination, truncating on overflow and
// always NUL-terminating in the last slot. Mirrors the C++:
//
//   strncpy(dst, src, sizeof(dst) - 1);
//   dst[sizeof(dst) - 1] = '\0';
//
// Note: `strncpy` zero-fills the tail when src is shorter than dst-1; we
// match that to keep byte-for-byte parity in case anyone hashes the buffer.
fn copy_into_fixed(dst: &mut [u8], src: &[u8]) {
    let max = dst.len() - 1;
    let src_effective = source_until_nul(src, max);
    let copy_len = src_effective.len().min(max);
    dst[..copy_len].copy_from_slice(&src_effective[..copy_len]);
    // Zero-fill the tail (strncpy semantics).
    for byte in &mut dst[copy_len..] {
        *byte = 0;
    }
}

// Return the prefix of `src` up to the first NUL byte (or up to `cap`,
// whichever comes first). C strings stop at NUL — match the C++ behaviour.
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

    // --- struct layout proof -------------------------------------------------

    #[test]
    fn fw_error_size_matches_cxx() {
        assert_eq!(core::mem::size_of::<FwError>(), 1 + 12 + 52);
    }

    // --- push ----------------------------------------------------------------

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
        assert_eq!(version, 3); // dup still bumps version
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
        assert_eq!(head, 1); // oldest evicted
        assert_eq!(code_of(&ring[0]), "C6"); // newest landed at the slot vacated by head
    }

    #[test]
    fn push_truncates_too_long_message() {
        let mut ring = blank_ring();
        let mut head = 0u8;
        let mut count = 0u8;
        let mut version = 0u32;

        // 60 chars — longer than MESSAGE_LEN (52). Must truncate at 51 + NUL.
        let long_msg = "x".repeat(60);
        push_one(&mut ring, &mut head, &mut count, &mut version, 1, "C", &long_msg);
        assert_eq!(message_of(&ring[0]).len(), MESSAGE_LEN - 1);
        assert_eq!(ring[0].message[MESSAGE_LEN - 1], 0);
    }

    // --- get_all -------------------------------------------------------------

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
        assert_eq!(code_of(&out[0]), "C5"); // newest
        assert_eq!(code_of(&out[1]), "C4");
    }

    #[test]
    fn get_all_handles_post_wrap_state() {
        // Fill ring, then push one more — head advances to 1, slot 0 holds
        // the newest. get_all must still walk newest-first across the wrap.
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
        // newest-first: C6, C5, C4, C3, C2, C1
        for (i, expected) in ["C6", "C5", "C4", "C3", "C2", "C1"].iter().enumerate() {
            assert_eq!(code_of(&out[i]), *expected, "row {i}");
        }
    }

    // --- dismiss_at ----------------------------------------------------------

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
        // The oldest (A) remains visible.
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
        // row = count - 1 = 2 → oldest (A)
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
        // row = 1 → middle (B)
        dismiss_at(&mut ring, &mut head, &mut count, &mut version, 1);
        assert_eq!(count, 2);
        let mut out = [FwError {
            source: 0,
            code: [0; CODE_LEN],
            message: [0; MESSAGE_LEN],
        }; 2];
        get_all(&ring, head, count, &mut out);
        // Newest-first: C, A (B removed)
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
        assert_eq!(version, v_before); // version did NOT advance
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
