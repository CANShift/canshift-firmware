pub const WORKSHOP_ENTROPY_BYTES: usize = 64;
pub const WORKSHOP_SSID_CAP: usize = 33;
pub const WORKSHOP_PASSWORD_CAP: usize = 64;
pub const WORKSHOP_PASSWORD_LEN: usize = 12;

const SSID_PREFIX: &[u8] = b"CANShift-";
const SSID_SUFFIX_LEN: usize = 4;
const SSID_MAX_LEN: usize = WORKSHOP_SSID_CAP - 1;
const PASSWORD_MIN_LEN: usize = 8;
const PASSWORD_MAX_LEN: usize = WORKSHOP_PASSWORD_CAP - 1;
const PRINTABLE_FIRST: u8 = 0x20;
const PRINTABLE_LAST: u8 = 0x7E;

const ALPHABET: &[u8] = b"abcdefghjkmnpqrstuvwxyz23456789";
const UNBIASED_LIMIT: usize = 256 - (256 % ALPHABET.len());

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Credentials {
    pub ssid: [u8; WORKSHOP_SSID_CAP],
    pub ssid_len: usize,
    pub password: [u8; WORKSHOP_PASSWORD_CAP],
    pub password_len: usize,
}

struct Draw<'a> {
    entropy: &'a [u8],
    next: usize,
}

impl Draw<'_> {
    fn symbol(&mut self) -> Option<u8> {
        while let Some(&byte) = self.entropy.get(self.next) {
            self.next += 1;
            if usize::from(byte) < UNBIASED_LIMIT {
                return Some(ALPHABET[usize::from(byte) % ALPHABET.len()]);
            }
        }
        None
    }

    fn fill(&mut self, out: &mut [u8], uppercase: bool) -> Option<()> {
        for slot in out.iter_mut() {
            let symbol = self.symbol()?;
            *slot = if uppercase {
                symbol.to_ascii_uppercase()
            } else {
                symbol
            };
        }
        Some(())
    }
}

#[must_use]
pub fn generate_credentials(entropy: &[u8]) -> Option<Credentials> {
    let mut creds = Credentials {
        ssid: [0; WORKSHOP_SSID_CAP],
        ssid_len: SSID_PREFIX.len() + SSID_SUFFIX_LEN,
        password: [0; WORKSHOP_PASSWORD_CAP],
        password_len: WORKSHOP_PASSWORD_LEN,
    };
    let mut draw = Draw { entropy, next: 0 };
    creds.ssid[..SSID_PREFIX.len()].copy_from_slice(SSID_PREFIX);
    draw.fill(&mut creds.ssid[SSID_PREFIX.len()..creds.ssid_len], true)?;
    draw.fill(&mut creds.password[..WORKSHOP_PASSWORD_LEN], false)?;
    Some(creds)
}

fn printable(bytes: &[u8]) -> bool {
    bytes
        .iter()
        .all(|&b| (PRINTABLE_FIRST..=PRINTABLE_LAST).contains(&b))
}

#[must_use]
pub fn credentials_valid(ssid: &[u8], password: &[u8]) -> bool {
    let ssid_fits = (1..=SSID_MAX_LEN).contains(&ssid.len());
    let password_fits = (PASSWORD_MIN_LEN..=PASSWORD_MAX_LEN).contains(&password.len());
    ssid_fits && password_fits && printable(ssid) && printable(password)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn counting_entropy() -> [u8; WORKSHOP_ENTROPY_BYTES] {
        core::array::from_fn(|i| i as u8)
    }

    fn generated() -> Credentials {
        generate_credentials(&counting_entropy()).unwrap()
    }

    #[test]
    fn the_network_name_is_prefixed_with_an_uppercase_suffix() {
        let creds = generated();
        assert_eq!(&creds.ssid[..creds.ssid_len], b"CANShift-ABCD");
    }

    #[test]
    fn the_password_follows_from_the_entropy() {
        let creds = generated();
        assert_eq!(&creds.password[..creds.password_len], b"efghjkmnpqrs");
    }

    #[test]
    fn generated_credentials_are_valid_wpa2() {
        let creds = generated();
        assert!(credentials_valid(
            &creds.ssid[..creds.ssid_len],
            &creds.password[..creds.password_len]
        ));
    }

    #[test]
    fn biased_bytes_are_skipped() {
        let mut entropy = counting_entropy();
        entropy[0] = 255;
        let creds = generate_credentials(&entropy).unwrap();
        assert_eq!(&creds.ssid[..creds.ssid_len], b"CANShift-BCDE");
    }

    #[test]
    fn ambiguous_symbols_never_appear() {
        let entropy: [u8; WORKSHOP_ENTROPY_BYTES] = core::array::from_fn(|i| (i * 37) as u8);
        let creds = generate_credentials(&entropy).unwrap();
        let password = &creds.password[..creds.password_len];
        for ambiguous in b"il1o0" {
            assert!(!password.contains(ambiguous));
        }
    }

    #[test]
    fn exhausted_entropy_fails_instead_of_repeating() {
        assert!(generate_credentials(&[0u8; 10]).is_none());
        assert!(generate_credentials(&[255u8; WORKSHOP_ENTROPY_BYTES]).is_none());
    }

    #[test]
    fn the_buffers_stay_nul_terminated() {
        let creds = generated();
        assert_eq!(creds.ssid[creds.ssid_len], 0);
        assert_eq!(creds.password[creds.password_len], 0);
    }

    #[test]
    fn a_short_password_is_rejected() {
        assert!(!credentials_valid(b"CANShift-ABCD", b"short"));
    }

    #[test]
    fn an_overlong_ssid_is_rejected() {
        assert!(!credentials_valid(&[b'a'; SSID_MAX_LEN + 1], b"longenough"));
        assert!(credentials_valid(&[b'a'; SSID_MAX_LEN], b"longenough"));
    }

    #[test]
    fn an_empty_ssid_is_rejected() {
        assert!(!credentials_valid(b"", b"longenough"));
    }

    #[test]
    fn control_bytes_are_rejected() {
        assert!(!credentials_valid(b"CANShift\n", b"longenough"));
        assert!(!credentials_valid(b"CANShift", b"long\x7fenough"));
    }

    #[test]
    fn the_longest_password_is_accepted() {
        assert!(credentials_valid(b"CANShift", &[b'x'; PASSWORD_MAX_LEN]));
        assert!(!credentials_valid(
            b"CANShift",
            &[b'x'; PASSWORD_MAX_LEN + 1]
        ));
    }
}
