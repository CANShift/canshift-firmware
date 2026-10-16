use core::ffi::{c_char, CStr};
use core::slice;

use crate::{
    board_supported, content_type, credentials_valid, generate_credentials, join_payload,
    motion_exit_due, next_state, offered, origin_allowed, resume_after_reboot, socket_admission,
    spa_route, tuner_path, MotionWatch, ResumeReason, WorkshopEvent, WorkshopState,
    DEFAULT_CONTENT_TYPE,
};

#[no_mangle]
pub extern "C" fn workshop_next_state_rs(state: u8, event: u8, is_offered: bool) -> u8 {
    let current = WorkshopState::from_u8(state);
    match WorkshopEvent::from_u8(event) {
        Some(event) => next_state(current, event, is_offered) as u8,
        None => current as u8,
    }
}

#[no_mangle]
pub extern "C" fn workshop_board_supported_rs(filesystem_bytes: u32) -> bool {
    board_supported(filesystem_bytes)
}

#[no_mangle]
pub extern "C" fn workshop_resume_after_reboot_rs(
    reason: u8,
    filesystem_bytes: u32,
    image_pending_verify: bool,
) -> bool {
    resume_after_reboot(
        ResumeReason::from_u8(reason),
        filesystem_bytes,
        image_pending_verify,
    )
}

#[no_mangle]
pub extern "C" fn workshop_offered_rs(
    filesystem_bytes: u32,
    speed_mapped: bool,
    speed_valid: bool,
    speed_kph: f32,
) -> bool {
    offered(filesystem_bytes, speed_mapped, speed_valid, speed_kph)
}

#[no_mangle]
pub unsafe extern "C" fn workshop_motion_exit_due_rs(
    watch: *mut MotionWatch,
    speed_valid: bool,
    speed_kph: f32,
    now_ms: u32,
) -> bool {
    let Some(watch) = (unsafe { watch.as_mut() }) else {
        return false;
    };
    motion_exit_due(watch, speed_valid, speed_kph, now_ms)
}

#[no_mangle]
pub extern "C" fn workshop_socket_admission_rs(
    has_client: bool,
    same_client: bool,
    transfer_in_flight: bool,
    last_activity_ms: u32,
    now_ms: u32,
) -> u8 {
    socket_admission(
        has_client,
        same_client,
        transfer_in_flight,
        last_activity_ms,
        now_ms,
    ) as u8
}

unsafe fn copy_out(src: &[u8], dst: *mut c_char, cap: usize) -> bool {
    if dst.is_null() || src.len() >= cap {
        return false;
    }
    let out = unsafe { slice::from_raw_parts_mut(dst.cast::<u8>(), cap) };
    out[..src.len()].copy_from_slice(src);
    out[src.len()] = 0;
    true
}

#[no_mangle]
pub unsafe extern "C" fn workshop_generate_credentials_rs(
    entropy: *const u8,
    entropy_len: usize,
    ssid: *mut c_char,
    ssid_cap: usize,
    password: *mut c_char,
    password_cap: usize,
) -> bool {
    if entropy.is_null() {
        return false;
    }
    let entropy = unsafe { slice::from_raw_parts(entropy, entropy_len) };
    let Some(creds) = generate_credentials(entropy) else {
        return false;
    };
    let ssid_ok = unsafe { copy_out(&creds.ssid[..creds.ssid_len], ssid, ssid_cap) };
    let password_ok = unsafe {
        copy_out(
            &creds.password[..creds.password_len],
            password,
            password_cap,
        )
    };
    ssid_ok && password_ok
}

unsafe fn bytes_of<'a>(text: *const c_char) -> Option<&'a [u8]> {
    if text.is_null() {
        return None;
    }
    Some(unsafe { CStr::from_ptr(text) }.to_bytes())
}

#[no_mangle]
pub unsafe extern "C" fn workshop_credentials_valid_rs(
    ssid: *const c_char,
    password: *const c_char,
) -> bool {
    match unsafe { (bytes_of(ssid), bytes_of(password)) } {
        (Some(ssid), Some(password)) => credentials_valid(ssid, password),
        _ => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn workshop_join_payload_rs(
    ssid: *const c_char,
    password: *const c_char,
    out: *mut c_char,
    out_cap: usize,
) -> usize {
    let (Some(ssid), Some(password)) = (unsafe { bytes_of(ssid) }, unsafe { bytes_of(password) })
    else {
        return 0;
    };
    if out.is_null() {
        return 0;
    }
    let out = unsafe { slice::from_raw_parts_mut(out.cast::<u8>(), out_cap) };
    join_payload(ssid, password, out).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn workshop_origin_allowed_rs(
    origin: *const c_char,
    ap_ip: *const c_char,
    host_name: *const c_char,
) -> bool {
    let origin = unsafe { bytes_of(origin) };
    let ap_ip = unsafe { bytes_of(ap_ip) }.unwrap_or(b"");
    let host_name = unsafe { bytes_of(host_name) }.unwrap_or(b"");
    origin_allowed(origin, ap_ip, host_name)
}

#[no_mangle]
pub unsafe extern "C" fn workshop_tuner_path_rs(
    uri: *const c_char,
    gzip: bool,
    out: *mut c_char,
    out_cap: usize,
) -> usize {
    let Some(uri) = (unsafe { bytes_of(uri) }) else {
        return 0;
    };
    if out.is_null() {
        return 0;
    }
    let out = unsafe { slice::from_raw_parts_mut(out.cast::<u8>(), out_cap) };
    tuner_path(uri, gzip, out).unwrap_or(0)
}

#[no_mangle]
pub unsafe extern "C" fn workshop_tuner_spa_route_rs(uri: *const c_char) -> bool {
    unsafe { bytes_of(uri) }.is_some_and(spa_route)
}

#[no_mangle]
pub unsafe extern "C" fn workshop_tuner_content_type_rs(uri: *const c_char) -> *const c_char {
    let mime = unsafe { bytes_of(uri) }.map_or(DEFAULT_CONTENT_TYPE, content_type);
    mime.as_ptr().cast::<c_char>()
}

#[cfg(test)]
mod tests {
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

    #[test]
    fn ffi_origin_check_treats_null_as_absent_and_refuses_foreign_pages() {
        let ap_ip = c"192.168.4.1";
        let host = c"canshift";
        unsafe {
            assert!(workshop_origin_allowed_rs(
                ptr::null(),
                ap_ip.as_ptr(),
                host.as_ptr()
            ));
            assert!(workshop_origin_allowed_rs(
                c"http://192.168.4.1".as_ptr(),
                ap_ip.as_ptr(),
                host.as_ptr()
            ));
            assert!(workshop_origin_allowed_rs(
                c"http://canshift.local".as_ptr(),
                ap_ip.as_ptr(),
                host.as_ptr()
            ));
            assert!(!workshop_origin_allowed_rs(
                c"http://evil.example".as_ptr(),
                ap_ip.as_ptr(),
                host.as_ptr()
            ));
            assert!(!workshop_origin_allowed_rs(
                c"http://192.168.4.1".as_ptr(),
                ptr::null(),
                ptr::null()
            ));
        }
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
            unsafe {
                workshop_tuner_path_rs(c"/../x".as_ptr(), false, out.as_mut_ptr(), out.len())
            },
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
}
