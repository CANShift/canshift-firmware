const SCHEME: &[u8] = b"http://";
const MDNS_SUFFIX: &[u8] = b".local";

fn strip_scheme(origin: &[u8]) -> Option<&[u8]> {
    if origin.len() < SCHEME.len() {
        return None;
    }
    let (scheme, host) = origin.split_at(SCHEME.len());
    scheme.eq_ignore_ascii_case(SCHEME).then_some(host)
}

fn is_mdns_name(host: &[u8], host_name: &[u8]) -> bool {
    let Some(split) = host.len().checked_sub(MDNS_SUFFIX.len()) else {
        return false;
    };
    let (name, suffix) = host.split_at(split);
    !host_name.is_empty()
        && suffix.eq_ignore_ascii_case(MDNS_SUFFIX)
        && name.eq_ignore_ascii_case(host_name)
}

#[must_use]
pub fn origin_allowed(origin: Option<&[u8]>, ap_ip: &[u8], host_name: &[u8]) -> bool {
    let Some(origin) = origin else {
        return true;
    };
    let Some(host) = strip_scheme(origin) else {
        return false;
    };
    (!ap_ip.is_empty() && host.eq_ignore_ascii_case(ap_ip)) || is_mdns_name(host, host_name)
}

#[cfg(test)]
mod tests {
    use super::*;

    const AP_IP: &[u8] = b"192.168.4.1";
    const HOST: &[u8] = b"canshift";

    fn allowed(origin: &[u8]) -> bool {
        origin_allowed(Some(origin), AP_IP, HOST)
    }

    #[test]
    fn a_non_browser_client_sends_no_origin() {
        assert!(origin_allowed(None, AP_IP, HOST));
    }

    #[test]
    fn the_dash_served_tuner_is_allowed_by_ip_or_mdns_name() {
        assert!(allowed(b"http://192.168.4.1"));
        assert!(allowed(b"http://canshift.local"));
        assert!(allowed(b"HTTP://CANShift.LOCAL"));
    }

    #[test]
    fn a_foreign_page_is_refused() {
        assert!(!allowed(b"http://evil.example"));
        assert!(!allowed(b"http://192.168.4.2"));
        assert!(!allowed(b"http://canshift.local.evil.example"));
        assert!(!allowed(b"http://evil.canshift.local"));
        assert!(!allowed(b"http://localhost:5173"));
    }

    #[test]
    fn scheme_port_and_path_must_match_exactly() {
        assert!(!allowed(b"https://192.168.4.1"));
        assert!(!allowed(b"http://192.168.4.1:80"));
        assert!(!allowed(b"http://192.168.4.1/"));
        assert!(!allowed(b"ws://192.168.4.1"));
    }

    #[test]
    fn opaque_empty_and_truncated_origins_are_refused() {
        assert!(!allowed(b"null"));
        assert!(!allowed(b""));
        assert!(!allowed(b"http://"));
        assert!(!allowed(b"http:/"));
    }

    #[test]
    fn an_unknown_access_point_address_matches_nothing() {
        assert!(!origin_allowed(Some(b"http://"), b"", HOST));
        assert!(!origin_allowed(Some(b"http://.local"), AP_IP, b""));
        assert!(origin_allowed(Some(b"http://canshift.local"), b"", HOST));
    }
}
