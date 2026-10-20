use crate::table::{clear_entries, CanMonitor};

pub const CAN_MONITOR_FILTER_VERSION: u8 = 0x01;
pub const CAN_MONITOR_MAX_FILTERS: usize = 8;
pub const CAN_MONITOR_FILTER_HEADER_BYTES: usize = 2;
pub const CAN_MONITOR_FILTER_PAIR_BYTES: usize = 8;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct IdFilter {
    pub id: u32,
    pub mask: u32,
}

fn read_u32_le(bytes: &[u8]) -> u32 {
    u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])
}

fn pair_count(bytes: &[u8]) -> Option<usize> {
    let (&version, rest) = bytes.split_first()?;
    let (&count, pairs) = rest.split_first()?;
    let count = usize::from(count);
    let well_formed = version == CAN_MONITOR_FILTER_VERSION
        && count <= CAN_MONITOR_MAX_FILTERS
        && pairs.len() == count * CAN_MONITOR_FILTER_PAIR_BYTES;
    well_formed.then_some(count)
}

pub fn set_filter(monitor: &mut CanMonitor, bytes: &[u8]) -> bool {
    let Some(count) = pair_count(bytes) else {
        return false;
    };
    let (pairs, _) =
        bytes[CAN_MONITOR_FILTER_HEADER_BYTES..].as_chunks::<CAN_MONITOR_FILTER_PAIR_BYTES>();
    for (slot, pair) in monitor.filters.iter_mut().zip(pairs) {
        *slot = IdFilter {
            id: read_u32_le(&pair[..4]),
            mask: read_u32_le(&pair[4..]),
        };
    }
    monitor.filter_count = count as u8;
    clear_entries(monitor);
    true
}

pub fn accepts(monitor: &CanMonitor, id: u32) -> bool {
    let count = usize::from(monitor.filter_count).min(CAN_MONITOR_MAX_FILTERS);
    count == 0
        || monitor.filters[..count]
            .iter()
            .any(|filter| id & filter.mask == filter.id & filter.mask)
}
