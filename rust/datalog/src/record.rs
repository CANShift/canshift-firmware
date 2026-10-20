use crate::crc::crc16;
use crate::cursor::Cursor;

pub const DATALOG_MAGIC: [u8; 4] = *b"CSLG";
pub const DATALOG_FORMAT_VERSION: u8 = 1;
pub const DATALOG_PREAMBLE_BYTES: usize = 5;

pub const DATALOG_RECORD_HEADER: u8 = 1;
pub const DATALOG_RECORD_SAMPLE: u8 = 2;
pub const DATALOG_RECORD_LAP: u8 = 3;
pub const DATALOG_RECORD_END: u8 = 4;

pub const DATALOG_FRAME_PREFIX_BYTES: usize = 3;
pub const DATALOG_FRAME_CRC_BYTES: usize = 2;
pub const DATALOG_SIGNAL_SLOTS: usize = 64;

pub const DATALOG_MAX_SAMPLE_BYTES: usize = 273;
pub const DATALOG_MAX_HEADER_BYTES: usize = 2186;
pub const DATALOG_MAX_MARKER_BYTES: usize = 15;

#[must_use]
pub fn encode_preamble(out: &mut [u8]) -> usize {
    let mut cursor = Cursor::new(out);
    cursor.put(&DATALOG_MAGIC);
    cursor.put_u8(DATALOG_FORMAT_VERSION);
    cursor.finish().unwrap_or(0)
}

#[must_use]
pub fn encode_header(out: &mut [u8], sample_period_ms: u16, session: u16) -> usize {
    let catalog = signal_map::catalog();
    let Ok(count) = u8::try_from(catalog.len()) else {
        return 0;
    };
    encode_frame(out, DATALOG_RECORD_HEADER, |cursor| {
        cursor.put_u16(sample_period_ms);
        cursor.put_u16(session);
        cursor.put_u8(count);
        catalog
            .iter()
            .for_each(|&(name, id)| put_signal(cursor, id, name));
    })
}

#[must_use]
pub fn encode_sample(
    out: &mut [u8],
    t_ms: u32,
    mask: u64,
    values: &[f32; DATALOG_SIGNAL_SLOTS],
) -> usize {
    let kept = finite_mask(mask, values);
    encode_frame(out, DATALOG_RECORD_SAMPLE, |cursor| {
        cursor.put_u32(t_ms);
        cursor.put_u64(kept);
        values
            .iter()
            .enumerate()
            .filter(|&(slot, _)| has_slot(kept, slot))
            .for_each(|(_, &value)| cursor.put_f32(value));
    })
}

#[must_use]
pub fn encode_lap(out: &mut [u8], t_ms: u32, index: u16, lap_ms: u32) -> usize {
    encode_frame(out, DATALOG_RECORD_LAP, |cursor| {
        cursor.put_u32(t_ms);
        cursor.put_u16(index);
        cursor.put_u32(lap_ms);
    })
}

#[must_use]
pub fn encode_end(out: &mut [u8], t_ms: u32, dropped: u32) -> usize {
    encode_frame(out, DATALOG_RECORD_END, |cursor| {
        cursor.put_u32(t_ms);
        cursor.put_u32(dropped);
    })
}

fn put_signal(cursor: &mut Cursor, id: u8, name: &str) {
    let bytes = name.as_bytes();
    let Ok(len) = u8::try_from(bytes.len()) else {
        return;
    };
    cursor.put_u8(id);
    cursor.put_u8(len);
    cursor.put(bytes);
}

fn has_slot(mask: u64, slot: usize) -> bool {
    slot < DATALOG_SIGNAL_SLOTS && (mask >> slot) & 1 == 1
}

fn finite_mask(mask: u64, values: &[f32; DATALOG_SIGNAL_SLOTS]) -> u64 {
    values
        .iter()
        .enumerate()
        .filter(|&(slot, value)| has_slot(mask, slot) && value.is_finite())
        .fold(0, |kept, (slot, _)| kept | (1u64 << slot))
}

fn encode_frame(out: &mut [u8], kind: u8, body: impl FnOnce(&mut Cursor)) -> usize {
    let mut cursor = Cursor::new(out);
    cursor.put_u8(kind);
    cursor.put_u16(0);
    body(&mut cursor);
    let Some(end) = cursor.finish() else {
        return 0;
    };
    seal(out, end)
}

fn seal(out: &mut [u8], end: usize) -> usize {
    let Ok(len) = u16::try_from(end - DATALOG_FRAME_PREFIX_BYTES) else {
        return 0;
    };
    let total = end + DATALOG_FRAME_CRC_BYTES;
    if total > out.len() {
        return 0;
    }
    out[1..DATALOG_FRAME_PREFIX_BYTES].copy_from_slice(&len.to_le_bytes());
    let crc = crc16(&out[..end]);
    out[end..total].copy_from_slice(&crc.to_le_bytes());
    total
}

#[cfg(test)]
mod tests;
