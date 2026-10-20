pub const DATALOG_CRC_SEED: u16 = 0xFFFF;

const CRC_POLY: u16 = 0x1021;
const CRC_TOP_BIT: u16 = 0x8000;
const BITS_PER_BYTE: u32 = 8;

#[must_use]
pub fn crc16(bytes: &[u8]) -> u16 {
    bytes
        .iter()
        .fold(DATALOG_CRC_SEED, |crc, &byte| crc_byte(crc, byte))
}

fn crc_byte(crc: u16, byte: u8) -> u16 {
    (0..BITS_PER_BYTE).fold(crc ^ (u16::from(byte) << 8), |acc, _| {
        let shifted = acc << 1;
        if acc & CRC_TOP_BIT == 0 {
            shifted
        } else {
            shifted ^ CRC_POLY
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_the_ccitt_false_check_value() {
        assert_eq!(crc16(b"123456789"), 0x29B1);
    }

    #[test]
    fn empty_input_is_the_seed() {
        assert_eq!(crc16(&[]), DATALOG_CRC_SEED);
    }
}
