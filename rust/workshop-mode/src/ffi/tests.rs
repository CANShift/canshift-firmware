use super::*;
use crate::{
    WORKSHOP_ENTROPY_BYTES, WORKSHOP_EVENT_ENTERED, WORKSHOP_EVENT_REQUEST,
    WORKSHOP_JOIN_PAYLOAD_CAP, WORKSHOP_PASSWORD_CAP, WORKSHOP_SSID_CAP, WORKSHOP_STATE_ACTIVE,
    WORKSHOP_STATE_DRIVING, WORKSHOP_STATE_ENTERING,
};
use core::ptr;

fn text(buf: &[c_char]) -> &str {
    unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap()
}

#[test]
fn ffi_walks_driving_to_active() {
    let entering = workshop_next_state_rs(WORKSHOP_STATE_DRIVING, WORKSHOP_EVENT_REQUEST, true);
    assert_eq!(entering, WORKSHOP_STATE_ENTERING);
    assert_eq!(
        workshop_next_state_rs(entering, WORKSHOP_EVENT_ENTERED, true),
        WORKSHOP_STATE_ACTIVE
    );
}

#[test]
fn ffi_unknown_event_keeps_the_state() {
    assert_eq!(
        workshop_next_state_rs(WORKSHOP_STATE_ENTERING, 200, true),
        WORKSHOP_STATE_ENTERING
    );
}

#[test]
fn ffi_offered_matches_the_rule() {
    assert!(workshop_board_supported_rs(8 * 1024 * 1024));
    assert!(!workshop_board_supported_rs(832 * 1024));
    assert!(workshop_offered_rs(8 * 1024 * 1024, true, true, 0.0));
    assert!(!workshop_offered_rs(832 * 1024, false, false, 0.0));
}

#[test]
fn ffi_generates_terminated_credentials() {
    let entropy: [u8; WORKSHOP_ENTROPY_BYTES] = core::array::from_fn(|i| i as u8);
    let mut ssid = [0 as c_char; WORKSHOP_SSID_CAP];
    let mut password = [0 as c_char; WORKSHOP_PASSWORD_CAP];
    let ok = unsafe {
        workshop_generate_credentials_rs(
            entropy.as_ptr(),
            entropy.len(),
            ssid.as_mut_ptr(),
            ssid.len(),
            password.as_mut_ptr(),
            password.len(),
        )
    };
    assert!(ok);
    assert_eq!(text(&ssid), "CANShift-ABCD");
    assert_eq!(text(&password), "efghjkmnpqrs");
}

#[test]
fn ffi_generation_refuses_a_short_buffer() {
    let entropy: [u8; WORKSHOP_ENTROPY_BYTES] = core::array::from_fn(|i| i as u8);
    let mut ssid = [0 as c_char; 4];
    let mut password = [0 as c_char; WORKSHOP_PASSWORD_CAP];
    let ok = unsafe {
        workshop_generate_credentials_rs(
            entropy.as_ptr(),
            entropy.len(),
            ssid.as_mut_ptr(),
            ssid.len(),
            password.as_mut_ptr(),
            password.len(),
        )
    };
    assert!(!ok);
}

#[test]
fn ffi_null_pointers_fail_closed() {
    let mut out = [0 as c_char; WORKSHOP_JOIN_PAYLOAD_CAP];
    let ssid = c"CANShift-ABCD";
    unsafe {
        assert!(!workshop_generate_credentials_rs(
            ptr::null(),
            0,
            out.as_mut_ptr(),
            out.len(),
            out.as_mut_ptr(),
            out.len()
        ));
        assert!(!workshop_credentials_valid_rs(ptr::null(), ssid.as_ptr()));
        assert_eq!(
            workshop_join_payload_rs(ssid.as_ptr(), ptr::null(), out.as_mut_ptr(), out.len()),
            0
        );
        assert_eq!(
            workshop_join_payload_rs(ssid.as_ptr(), ssid.as_ptr(), ptr::null_mut(), 8),
            0
        );
    }
}

#[test]
fn ffi_validates_and_encodes_stored_credentials() {
    let ssid = c"CANShift-K7QX";
    let password = c"abcdefgh2345";
    let mut out = [0 as c_char; WORKSHOP_JOIN_PAYLOAD_CAP];
    unsafe {
        assert!(workshop_credentials_valid_rs(
            ssid.as_ptr(),
            password.as_ptr()
        ));
        let len = workshop_join_payload_rs(
            ssid.as_ptr(),
            password.as_ptr(),
            out.as_mut_ptr(),
            out.len(),
        );
        assert_eq!(len, 43);
    }
    assert_eq!(text(&out), "WIFI:T:WPA;S:CANShift-K7QX;P:abcdefgh2345;;");
}

fn origin_allowed(origin: *const c_char, ap_ip: *const c_char, host: *const c_char) -> bool {
    unsafe { workshop_origin_allowed_rs(origin, ap_ip, host) }
}

#[test]
fn ffi_origin_check_treats_null_as_absent_and_refuses_foreign_pages() {
    let ap_ip = c"192.168.4.1".as_ptr();
    let host = c"canshift".as_ptr();
    assert!(origin_allowed(ptr::null(), ap_ip, host));
    assert!(origin_allowed(c"http://192.168.4.1".as_ptr(), ap_ip, host));
    assert!(origin_allowed(
        c"http://canshift.local".as_ptr(),
        ap_ip,
        host
    ));
    assert!(!origin_allowed(
        c"http://evil.example".as_ptr(),
        ap_ip,
        host
    ));
    assert!(!origin_allowed(
        c"http://192.168.4.1".as_ptr(),
        ptr::null(),
        ptr::null()
    ));
}

#[test]
fn ffi_maps_tuner_assets() {
    let mut out = [0 as c_char; crate::WORKSHOP_TUNER_PATH_CAP];
    let len = unsafe {
        workshop_tuner_path_rs(c"/app.js?v=1".as_ptr(), true, out.as_mut_ptr(), out.len())
    };
    assert_eq!(len, 16);
    assert_eq!(text(&out), "/tuner/app.js.gz");
    assert_eq!(
        unsafe { workshop_tuner_path_rs(c"/../x".as_ptr(), false, out.as_mut_ptr(), out.len()) },
        0
    );
}

#[test]
fn ffi_answers_spa_and_content_type() {
    unsafe {
        assert!(workshop_tuner_spa_route_rs(c"/vehicle".as_ptr()));
        assert!(!workshop_tuner_spa_route_rs(c"/app.js".as_ptr()));
        assert!(!workshop_tuner_spa_route_rs(ptr::null()));
        let mime = CStr::from_ptr(workshop_tuner_content_type_rs(c"/".as_ptr()));
        assert_eq!(mime.to_str().unwrap(), "text/html; charset=utf-8");
        let fallback = CStr::from_ptr(workshop_tuner_content_type_rs(ptr::null()));
        assert_eq!(fallback.to_str().unwrap(), "application/octet-stream");
    }
}
