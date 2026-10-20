use crate::table::{CanMonitor, MonitorEntry};

pub const CAN_MONITOR_FRAME_VERSION: u8 = 0x01;
pub const CAN_MONITOR_HEADER_BYTES: usize = 8;
pub const CAN_MONITOR_ENTRY_FIXED_BYTES: usize = 9;
pub const CAN_MONITOR_MAX_FRAME_BYTES: usize = 244;
pub const CAN_MONITOR_FLAG_OVERFLOW: u8 = 0x01;
pub const CAN_MONITOR_FLAG_FILTERED: u8 = 0x02;

fn flags_of(monitor: &CanMonitor) -> u8 {
    let overflow = if monitor.overflow {
        CAN_MONITOR_FLAG_OVERFLOW
    } else {
        0
    };
    let filtered = if monitor.filter_count > 0 {
        CAN_MONITOR_FLAG_FILTERED
    } else {
        0
    };
    overflow | filtered
}

fn entry_bytes(entry: &MonitorEntry) -> usize {
    CAN_MONITOR_ENTRY_FIXED_BYTES + usize::from(entry.dlc)
}

fn write_entry(out: &mut [u8], entry: &MonitorEntry, now_ms: u32) {
    let age = now_ms.wrapping_sub(entry.last_ms).min(u32::from(u16::MAX)) as u16;
    out[..4].copy_from_slice(&entry.id.to_le_bytes());
    out[4] = entry.dlc;
    out[5..7].copy_from_slice(&entry.count.to_le_bytes());
    out[7..9].copy_from_slice(&age.to_le_bytes());
    out[CAN_MONITOR_ENTRY_FIXED_BYTES..entry_bytes(entry)]
        .copy_from_slice(&entry.data[..usize::from(entry.dlc)]);
}

fn take_entry(entry: &mut MonitorEntry) {
    entry.dirty = false;
    entry.count = 0;
}

fn write_entries(monitor: &mut CanMonitor, out: &mut [u8], now_ms: u32) -> (usize, u8) {
    let used = usize::from(monitor.used);
    let start = usize::from(monitor.cursor) % used;
    let mut offset = CAN_MONITOR_HEADER_BYTES;
    let mut count = 0u8;
    for step in 0..used {
        let index = (start + step) % used;
        let entry = &mut monitor.entries[index];
        if !entry.dirty {
            continue;
        }
        if offset + entry_bytes(entry) > out.len() {
            monitor.cursor = index as u16;
            return (offset, count);
        }
        write_entry(&mut out[offset..], entry, now_ms);
        offset += entry_bytes(entry);
        count += 1;
        take_entry(entry);
    }
    (offset, count)
}

pub fn encode(monitor: &mut CanMonitor, now_ms: u32, out: &mut [u8]) -> usize {
    let cap = out.len().min(CAN_MONITOR_MAX_FRAME_BYTES);
    if monitor.used == 0 || cap < CAN_MONITOR_HEADER_BYTES {
        return 0;
    }
    let out = &mut out[..cap];
    let (len, count) = write_entries(monitor, out, now_ms);
    if count == 0 {
        return 0;
    }
    out[0] = CAN_MONITOR_FRAME_VERSION;
    out[1] = count;
    out[2] = flags_of(monitor);
    out[3] = 0;
    out[4..8].copy_from_slice(&now_ms.to_le_bytes());
    len
}

#[cfg(test)]
mod tests;
