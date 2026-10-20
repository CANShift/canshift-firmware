#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

mod crc;
mod cursor;
mod record;
mod ring;

pub use crc::{crc16, DATALOG_CRC_SEED};
pub use record::{
    encode_end, encode_header, encode_lap, encode_preamble, encode_sample, DATALOG_FORMAT_VERSION,
    DATALOG_FRAME_CRC_BYTES, DATALOG_FRAME_PREFIX_BYTES, DATALOG_MAGIC, DATALOG_MAX_HEADER_BYTES,
    DATALOG_MAX_MARKER_BYTES, DATALOG_MAX_SAMPLE_BYTES, DATALOG_PREAMBLE_BYTES, DATALOG_RECORD_END,
    DATALOG_RECORD_HEADER, DATALOG_RECORD_LAP, DATALOG_RECORD_SAMPLE, DATALOG_SIGNAL_SLOTS,
};
pub use ring::Ring;
