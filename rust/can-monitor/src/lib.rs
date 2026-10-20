#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod filter;
mod frame;
mod table;
#[cfg(test)]
mod vectors;

pub use filter::{
    accepts, set_filter, IdFilter, CAN_MONITOR_FILTER_HEADER_BYTES, CAN_MONITOR_FILTER_PAIR_BYTES,
    CAN_MONITOR_FILTER_VERSION, CAN_MONITOR_MAX_FILTERS,
};
pub use frame::{
    encode, CAN_MONITOR_ENTRY_FIXED_BYTES, CAN_MONITOR_FLAG_FILTERED, CAN_MONITOR_FLAG_OVERFLOW,
    CAN_MONITOR_FRAME_VERSION, CAN_MONITOR_HEADER_BYTES, CAN_MONITOR_MAX_FRAME_BYTES,
};
pub use table::{
    offer, reset, CanMonitor, MonitorEntry, CAN_MONITOR_EXTENDED_ID, CAN_MONITOR_ID_CAPACITY,
    CAN_MONITOR_MAX_DATA_BYTES,
};
