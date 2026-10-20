use crate::filter::{accepts, IdFilter, CAN_MONITOR_MAX_FILTERS};

pub const CAN_MONITOR_ID_CAPACITY: usize = 64;
pub const CAN_MONITOR_MAX_DATA_BYTES: usize = 8;
pub const CAN_MONITOR_EXTENDED_ID: u32 = 0x8000_0000;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct MonitorEntry {
    pub id: u32,
    pub last_ms: u32,
    pub count: u16,
    pub dlc: u8,
    pub dirty: bool,
    pub data: [u8; CAN_MONITOR_MAX_DATA_BYTES],
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct CanMonitor {
    pub entries: [MonitorEntry; CAN_MONITOR_ID_CAPACITY],
    pub filters: [IdFilter; CAN_MONITOR_MAX_FILTERS],
    pub used: u16,
    pub cursor: u16,
    pub filter_count: u8,
    pub overflow: bool,
    pub reserved: [u8; 2],
}

impl Default for CanMonitor {
    fn default() -> Self {
        Self {
            entries: [MonitorEntry::default(); CAN_MONITOR_ID_CAPACITY],
            filters: [IdFilter::default(); CAN_MONITOR_MAX_FILTERS],
            used: 0,
            cursor: 0,
            filter_count: 0,
            overflow: false,
            reserved: [0; 2],
        }
    }
}

pub fn reset(monitor: &mut CanMonitor) {
    *monitor = CanMonitor::default();
}

pub(crate) fn clear_entries(monitor: &mut CanMonitor) {
    monitor.used = 0;
    monitor.cursor = 0;
    monitor.overflow = false;
}

fn slot_for(monitor: &mut CanMonitor, id: u32) -> Option<&mut MonitorEntry> {
    let used = usize::from(monitor.used).min(CAN_MONITOR_ID_CAPACITY);
    if let Some(index) = monitor.entries[..used]
        .iter()
        .position(|entry| entry.id == id)
    {
        return Some(&mut monitor.entries[index]);
    }
    if used == CAN_MONITOR_ID_CAPACITY {
        monitor.overflow = true;
        return None;
    }
    monitor.used += 1;
    monitor.entries[used] = MonitorEntry {
        id,
        ..MonitorEntry::default()
    };
    Some(&mut monitor.entries[used])
}

pub fn offer(monitor: &mut CanMonitor, id: u32, data: &[u8], now_ms: u32) -> bool {
    if !accepts(monitor, id) {
        return false;
    }
    let Some(entry) = slot_for(monitor, id) else {
        return false;
    };
    let dlc = data.len().min(CAN_MONITOR_MAX_DATA_BYTES);
    entry.data = [0; CAN_MONITOR_MAX_DATA_BYTES];
    entry.data[..dlc].copy_from_slice(&data[..dlc]);
    entry.dlc = dlc as u8;
    entry.count = entry.count.saturating_add(1);
    entry.last_ms = now_ms;
    entry.dirty = true;
    true
}

#[cfg(test)]
mod tests;
