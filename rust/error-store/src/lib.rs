#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

pub const CODE_LEN: usize = 12;
pub const MESSAGE_LEN: usize = 52;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct FwError {
    pub source: u8,
    pub code: [u8; CODE_LEN],
    pub message: [u8; MESSAGE_LEN],
}

const _: () = assert!(core::mem::size_of::<FwError>() == 1 + CODE_LEN + MESSAGE_LEN);

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

    if let Some(idx) = find_entry(ring, *head, *count, source, code) {
        copy_into_fixed(&mut ring[idx].message, message);
        *version = version.wrapping_add(1);
        return;
    }
    let entry = &mut ring[claim_slot(ring_size, head, count) as usize];
    entry.source = source;
    copy_into_fixed(&mut entry.code, code);
    copy_into_fixed(&mut entry.message, message);
    *version = version.wrapping_add(1);
}

fn find_entry(ring: &[FwError], head: u8, count: u8, source: u8, code: &[u8]) -> Option<usize> {
    let ring_size = ring.len() as u8;
    (0..count)
        .map(|i| (head.wrapping_add(i) % ring_size) as usize)
        .find(|&idx| ring[idx].source == source && code_matches(&ring[idx].code, code))
}

fn claim_slot(ring_size: u8, head: &mut u8, count: &mut u8) -> u8 {
    if *count < ring_size {
        let slot = head.wrapping_add(*count) % ring_size;
        *count += 1;
        return slot;
    }
    let slot = *head;
    *head = head.wrapping_add(1) % ring_size;
    slot
}

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

pub fn dismiss_at(ring: &mut [FwError], head: &mut u8, count: &mut u8, version: &mut u32, row: u8) {
    let ring_size = ring.len() as u8;
    if ring_size == 0 || row >= *count {
        return;
    }

    let pos = (*count - 1).wrapping_sub(row);

    if pos == 0 {
        *head = (head.wrapping_add(1)) % ring_size;
    } else if pos < count.saturating_sub(1) {
        close_gap(ring, *head, *count, pos);
    }

    *count -= 1;
    *version = version.wrapping_add(1);
}

fn close_gap(ring: &mut [FwError], head: u8, count: u8, pos: u8) {
    let ring_size = ring.len() as u8;
    for i in pos..count.saturating_sub(1) {
        let dst = (head.wrapping_add(i)) % ring_size;
        let src = (head.wrapping_add(i + 1)) % ring_size;
        ring[dst as usize] = ring[src as usize];
    }
}

fn code_matches(stored: &[u8; CODE_LEN], incoming: &[u8]) -> bool {
    let mut key = [0u8; CODE_LEN];
    copy_into_fixed(&mut key, incoming);
    *stored == key
}

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
mod tests;
