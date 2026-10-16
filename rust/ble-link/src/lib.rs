#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

pub const PAIRING_DEADLINE_MS: u32 = 20_000;

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct LinkSecurity {
    pub encrypted: bool,
    pub authenticated: bool,
}

impl LinkSecurity {
    #[must_use]
    pub const fn new(encrypted: bool, authenticated: bool) -> Self {
        Self {
            encrypted,
            authenticated,
        }
    }

    #[must_use]
    pub const fn admitted(self) -> bool {
        self.encrypted && self.authenticated
    }

    #[must_use]
    pub const fn encrypted_without_passkey(self) -> bool {
        self.encrypted && !self.authenticated
    }
}

#[must_use]
pub const fn subscription_refused(link: LinkSecurity) -> bool {
    link.encrypted_without_passkey()
}

#[must_use]
pub const fn pairing_overdue(link: LinkSecurity, connected_ms: u32) -> bool {
    !link.admitted() && connected_ms >= PAIRING_DEADLINE_MS
}

#[cfg(test)]
mod tests {
    use super::*;

    const PLAIN: LinkSecurity = LinkSecurity::new(false, false);
    const JUST_WORKS: LinkSecurity = LinkSecurity::new(true, false);
    const PASSKEY: LinkSecurity = LinkSecurity::new(true, true);
    const AUTHENTICATED_BUT_OPEN: LinkSecurity = LinkSecurity::new(false, true);

    #[test]
    fn only_a_passkey_pairing_is_admitted() {
        assert!(PASSKEY.admitted());
        assert!(!JUST_WORKS.admitted());
        assert!(!PLAIN.admitted());
        assert!(!AUTHENTICATED_BUT_OPEN.admitted());
    }

    #[test]
    fn a_subscription_over_a_just_works_bond_is_refused() {
        assert!(subscription_refused(JUST_WORKS));
    }

    #[test]
    fn a_subscription_before_pairing_waits_for_the_passkey() {
        assert!(!subscription_refused(PLAIN));
        assert!(!subscription_refused(PASSKEY));
    }

    #[test]
    fn an_unauthenticated_peer_is_overdue_at_the_deadline() {
        assert!(!pairing_overdue(PLAIN, PAIRING_DEADLINE_MS - 1));
        assert!(pairing_overdue(PLAIN, PAIRING_DEADLINE_MS));
        assert!(pairing_overdue(JUST_WORKS, PAIRING_DEADLINE_MS));
        assert!(pairing_overdue(PLAIN, u32::MAX));
    }

    #[test]
    fn an_admitted_peer_is_never_overdue() {
        assert!(!pairing_overdue(PASSKEY, 0));
        assert!(!pairing_overdue(PASSKEY, u32::MAX));
    }

    #[test]
    fn a_fresh_connection_is_not_overdue() {
        assert!(!pairing_overdue(PLAIN, 0));
    }
}
