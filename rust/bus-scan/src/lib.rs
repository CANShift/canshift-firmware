#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod picker;
mod scan;
mod seen_ids;

pub use picker::{pick, Pick, BUS_SCAN_ERROR_RATIO, BUS_SCAN_MIN_FRAMES};
pub use scan::{
    abort, begin, fail, on_frame, queue, tick, BusScan, Probe, BUS_SCAN_ACTION_INSTALL,
    BUS_SCAN_ACTION_NONE, BUS_SCAN_ACTION_RESTORE, BUS_SCAN_COLLECT_MS, BUS_SCAN_OUTCOME_DETECTED,
    BUS_SCAN_OUTCOME_FAILED, BUS_SCAN_OUTCOME_NONE, BUS_SCAN_OUTCOME_SILENT,
    BUS_SCAN_OUTCOME_UNCLEAR, BUS_SCAN_PHASE_COLLECTING, BUS_SCAN_PHASE_DONE, BUS_SCAN_PHASE_IDLE,
    BUS_SCAN_PHASE_PROBING, BUS_SCAN_PROBE_MS, BUS_SCAN_RATES_KBPS, BUS_SCAN_RATE_COUNT,
};
pub use seen_ids::{record_id, SeenId, BUS_SCAN_ID_CAPACITY};
