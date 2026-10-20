#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod crc;
mod cursor;
mod lap_watch;
mod names;
mod record;
mod ring;
mod session;

pub use crc::{crc16, DATALOG_CRC_SEED};
pub use lap_watch::{observe, rebase, LapWatch};
pub use names::{is_log_name, log_name, DATALOG_NAME_BYTES};
pub use record::{
    encode_end, encode_header, encode_lap, encode_preamble, encode_sample, DATALOG_FORMAT_VERSION,
    DATALOG_FRAME_CRC_BYTES, DATALOG_FRAME_PREFIX_BYTES, DATALOG_MAGIC, DATALOG_MAX_HEADER_BYTES,
    DATALOG_MAX_MARKER_BYTES, DATALOG_MAX_SAMPLE_BYTES, DATALOG_PREAMBLE_BYTES, DATALOG_RECORD_END,
    DATALOG_RECORD_HEADER, DATALOG_RECORD_LAP, DATALOG_RECORD_SAMPLE, DATALOG_SIGNAL_SLOTS,
};
pub use ring::Ring;
pub use session::{
    halt, step, Session, SessionInputs, DATALOG_ACTION_CLOSE, DATALOG_ACTION_NONE,
    DATALOG_ACTION_OPEN, DATALOG_CLOSE_AFTER_SILENT_S, DATALOG_SESSION_HALTED,
    DATALOG_SESSION_IDLE, DATALOG_SESSION_OPEN,
};
