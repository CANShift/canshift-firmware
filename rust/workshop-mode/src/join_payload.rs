pub const WORKSHOP_JOIN_PAYLOAD_CAP: usize = 224;

const HEAD: &[u8] = b"WIFI:T:WPA;S:";
const PASSWORD_FIELD: &[u8] = b";P:";
const TAIL: &[u8] = b";;";
const ESCAPE: u8 = b'\\';
const RESERVED: &[u8] = b"\\;,:\"";

struct Writer<'a> {
    out: &'a mut [u8],
    len: usize,
}

impl Writer<'_> {
    fn push(&mut self, byte: u8) -> Option<()> {
        *self.out.get_mut(self.len)? = byte;
        self.len += 1;
        Some(())
    }

    fn raw(&mut self, bytes: &[u8]) -> Option<()> {
        bytes.iter().try_for_each(|&b| self.push(b))
    }

    fn escaped(&mut self, bytes: &[u8]) -> Option<()> {
        for &byte in bytes {
            if RESERVED.contains(&byte) {
                self.push(ESCAPE)?;
            }
            self.push(byte)?;
        }
        Some(())
    }
}

#[must_use]
pub fn join_payload(ssid: &[u8], password: &[u8], out: &mut [u8]) -> Option<usize> {
    let mut writer = Writer { out, len: 0 };
    writer.raw(HEAD)?;
    writer.escaped(ssid)?;
    writer.raw(PASSWORD_FIELD)?;
    writer.escaped(password)?;
    writer.raw(TAIL)?;
    let len = writer.len;
    writer.push(0)?;
    Some(len)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn payload(ssid: &[u8], password: &[u8]) -> Option<Vec<u8>> {
        let mut out = [0u8; WORKSHOP_JOIN_PAYLOAD_CAP];
        let len = join_payload(ssid, password, &mut out)?;
        Some(out[..len].to_vec())
    }

    #[test]
    fn a_plain_network_follows_the_wifi_uri_shape() {
        assert_eq!(
            payload(b"CANShift-K7QX", b"abcdefgh2345").unwrap(),
            b"WIFI:T:WPA;S:CANShift-K7QX;P:abcdefgh2345;;"
        );
    }

    #[test]
    fn reserved_characters_are_backslash_escaped() {
        assert_eq!(
            payload(b"a;b,c", b"p:q\"r\\s").unwrap(),
            b"WIFI:T:WPA;S:a\\;b\\,c;P:p\\:q\\\"r\\\\s;;"
        );
    }

    #[test]
    fn the_output_is_nul_terminated() {
        let mut out = [0xAAu8; WORKSHOP_JOIN_PAYLOAD_CAP];
        let len = join_payload(b"net", b"password", &mut out).unwrap();
        assert_eq!(out[len], 0);
    }

    #[test]
    fn the_worst_case_credentials_fit_the_cap() {
        let ssid = [b';'; 32];
        let password = [b'\\'; 63];
        assert!(payload(&ssid, &password).is_some());
    }

    #[test]
    fn a_short_buffer_fails_instead_of_truncating() {
        let mut out = [0u8; 16];
        assert!(join_payload(b"CANShift-K7QX", b"abcdefgh2345", &mut out).is_none());
    }

    #[test]
    fn room_for_the_text_but_not_the_terminator_fails() {
        let expected = b"WIFI:T:WPA;S:n;P:password;;";
        let mut out = [0u8; 27];
        assert_eq!(expected.len(), out.len());
        assert!(join_payload(b"n", b"password", &mut out).is_none());
    }
}
