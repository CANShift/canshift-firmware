use crate::{pairing_overdue, subscription_refused, LinkSecurity};

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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::PAIRING_DEADLINE_MS;

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
}
