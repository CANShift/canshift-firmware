use crate::{
    advertising_backs_off, pairing_overdue, subscription_refused, LinkSecurity, PasskeyPromptBudget,
};

#[no_mangle]
pub extern "C" fn ble_link_admitted_rs(encrypted: bool, authenticated: bool) -> bool {
    LinkSecurity::new(encrypted, authenticated).admitted()
}

#[no_mangle]
pub extern "C" fn ble_link_subscription_refused_rs(encrypted: bool, authenticated: bool) -> bool {
    subscription_refused(LinkSecurity::new(encrypted, authenticated))
}

#[no_mangle]
pub extern "C" fn ble_link_pairing_overdue_rs(
    encrypted: bool,
    authenticated: bool,
    connected_ms: u32,
) -> bool {
    pairing_overdue(LinkSecurity::new(encrypted, authenticated), connected_ms)
}

#[no_mangle]
pub extern "C" fn ble_link_advertising_backs_off_rs(advertising_ms: u32) -> bool {
    advertising_backs_off(advertising_ms)
}

#[no_mangle]
pub unsafe extern "C" fn ble_link_passkey_prompt_admit_rs(
    budget: *mut PasskeyPromptBudget,
    now_ms: u32,
) -> bool {
    if budget.is_null() {
        return false;
    }
    unsafe { &mut *budget }.admit(now_ms)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{ADVERTISING_FAST_WINDOW_MS, PAIRING_DEADLINE_MS, PASSKEY_PROMPTS_PER_WINDOW};

    #[test]
    fn ffi_admits_only_the_authenticated_link() {
        assert!(ble_link_admitted_rs(true, true));
        assert!(!ble_link_admitted_rs(true, false));
        assert!(!ble_link_admitted_rs(false, false));
    }

    #[test]
    fn ffi_refuses_the_just_works_subscriber_only() {
        assert!(ble_link_subscription_refused_rs(true, false));
        assert!(!ble_link_subscription_refused_rs(false, false));
        assert!(!ble_link_subscription_refused_rs(true, true));
    }

    #[test]
    fn ffi_deadline_matches_the_rule() {
        assert!(!ble_link_pairing_overdue_rs(
            false,
            false,
            PAIRING_DEADLINE_MS - 1
        ));
        assert!(ble_link_pairing_overdue_rs(
            false,
            false,
            PAIRING_DEADLINE_MS
        ));
        assert!(!ble_link_pairing_overdue_rs(true, true, u32::MAX));
    }

    #[test]
    fn ffi_backoff_matches_the_rule() {
        assert!(!ble_link_advertising_backs_off_rs(
            ADVERTISING_FAST_WINDOW_MS - 1
        ));
        assert!(ble_link_advertising_backs_off_rs(
            ADVERTISING_FAST_WINDOW_MS
        ));
    }

    #[test]
    fn ffi_refuses_a_null_budget() {
        assert!(!unsafe { ble_link_passkey_prompt_admit_rs(core::ptr::null_mut(), 0) });
    }

    #[test]
    fn ffi_prompt_budget_matches_the_rule() {
        let mut budget = PasskeyPromptBudget::default();
        for _ in 0..PASSKEY_PROMPTS_PER_WINDOW {
            assert!(unsafe { ble_link_passkey_prompt_admit_rs(&mut budget, 0) });
        }
        assert!(!unsafe { ble_link_passkey_prompt_admit_rs(&mut budget, 0) });
    }
}
