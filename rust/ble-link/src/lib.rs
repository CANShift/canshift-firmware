#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "ffi")]
mod ffi;

pub const PAIRING_DEADLINE_MS: u32 = 20_000;
pub const PASSKEY_PROMPTS_PER_WINDOW: u8 = 3;
pub const PASSKEY_PROMPT_WINDOW_MS: u32 = 60_000;

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

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub struct PasskeyPromptBudget {
    pub window_start_ms: u32,
    pub prompts: u8,
    pub _pad: [u8; 3],
}

impl PasskeyPromptBudget {
    pub fn admit(&mut self, now_ms: u32) -> bool {
        let window_open = self.prompts > 0
            && now_ms.wrapping_sub(self.window_start_ms) < PASSKEY_PROMPT_WINDOW_MS;
        if !window_open {
            self.window_start_ms = now_ms;
            self.prompts = 1;
            return true;
        }
        if self.prompts >= PASSKEY_PROMPTS_PER_WINDOW {
            return false;
        }
        self.prompts += 1;
        true
    }
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

    fn spend_window(budget: &mut PasskeyPromptBudget, start_ms: u32) {
        for i in 0..PASSKEY_PROMPTS_PER_WINDOW {
            assert!(budget.admit(start_ms.wrapping_add(u32::from(i))));
        }
    }

    #[test]
    fn prompts_past_the_budget_are_refused_inside_the_window() {
        let mut budget = PasskeyPromptBudget::default();
        spend_window(&mut budget, 1_000);
        assert!(!budget.admit(1_000 + PASSKEY_PROMPT_WINDOW_MS - 1));
    }

    #[test]
    fn the_budget_refills_once_the_window_has_passed() {
        let mut budget = PasskeyPromptBudget::default();
        spend_window(&mut budget, 1_000);
        assert!(budget.admit(1_000 + PASSKEY_PROMPT_WINDOW_MS));
        assert_eq!(budget.prompts, 1);
    }

    #[test]
    fn a_refused_prompt_does_not_extend_the_window() {
        let mut budget = PasskeyPromptBudget::default();
        spend_window(&mut budget, 0);
        assert!(!budget.admit(PASSKEY_PROMPT_WINDOW_MS / 2));
        assert!(budget.admit(PASSKEY_PROMPT_WINDOW_MS));
    }

    #[test]
    fn the_window_survives_the_millis_wrap() {
        let mut budget = PasskeyPromptBudget::default();
        spend_window(&mut budget, u32::MAX - 1);
        assert!(!budget.admit(10));
        assert!(budget.admit((u32::MAX - 1).wrapping_add(PASSKEY_PROMPT_WINDOW_MS)));
    }
}
