pub const DATALOG_NAME_BYTES: usize = 10;

const PREFIX: u8 = b'S';
const EXTENSION: &[u8] = b".csl";
const DIGITS: usize = 5;
const RADIX: u32 = 10;

#[must_use]
pub fn is_log_name(name: &[u8]) -> bool {
    let Some((&first, rest)) = name.split_first() else {
        return false;
    };
    let Some((digits, extension)) = rest.split_at_checked(DIGITS) else {
        return false;
    };
    first == PREFIX && digits.iter().all(u8::is_ascii_digit) && extension == EXTENSION
}

#[must_use]
pub fn log_name(session: u16, out: &mut [u8]) -> usize {
    let Some(name) = out.get_mut(..DATALOG_NAME_BYTES) else {
        return 0;
    };
    name[0] = PREFIX;
    let mut remaining = u32::from(session);
    name[1..=DIGITS].iter_mut().rev().for_each(|digit| {
        *digit = b'0' + (remaining % RADIX) as u8;
        remaining /= RADIX;
    });
    name[DIGITS + 1..].copy_from_slice(EXTENSION);
    DATALOG_NAME_BYTES
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_session_number_is_zero_padded() {
        let mut out = [0u8; DATALOG_NAME_BYTES];
        assert_eq!(log_name(42, &mut out), DATALOG_NAME_BYTES);
        assert_eq!(&out, b"S00042.csl");
        assert_eq!(log_name(u16::MAX, &mut out), DATALOG_NAME_BYTES);
        assert_eq!(&out, b"S65535.csl");
    }

    #[test]
    fn a_short_buffer_gets_no_name() {
        let mut out = [0u8; DATALOG_NAME_BYTES - 1];
        assert_eq!(log_name(1, &mut out), 0);
    }

    #[test]
    fn only_names_the_dash_writes_are_accepted() {
        assert!(is_log_name(b"S00001.csl"));
        [
            &b""[..],
            b"S0001.csl",
            b"S000001.csl",
            b"s00001.csl",
            b"S0000a.csl",
            b"S00001.txt",
            b"../S00001.csl",
            b"S00001.csl/",
            b"S00001.csl\0",
        ]
        .into_iter()
        .for_each(|name| assert!(!is_log_name(name), "{name:?}"));
    }
}
