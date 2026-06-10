use crate::{signal_id_from_name, SignalId, SIGNAL_COUNT};
use core::slice;

// Bounds the read so a missing NUL can't run off into adjacent memory.
const MAX_NAME_LEN: usize = 32;

/// # Safety
/// Caller guarantees `name` is null OR points to readable memory; we cap the
/// scan at MAX_NAME_LEN. Returns SIGNAL_COUNT on null/empty/unknown/too-long.
#[no_mangle]
pub unsafe extern "C" fn signal_id_from_name_rs(name: *const core::ffi::c_char) -> SignalId {
    if name.is_null() {
        return SIGNAL_COUNT;
    }
    let bytes = unsafe { slice::from_raw_parts(name.cast::<u8>(), MAX_NAME_LEN) };
    let len = match bytes.iter().position(|&b| b == 0) {
        Some(n) => n,
        None => return SIGNAL_COUNT,
    };
    if len == 0 {
        return SIGNAL_COUNT;
    }
    match core::str::from_utf8(&bytes[..len]) {
        Ok(s) => signal_id_from_name(s),
        Err(_) => SIGNAL_COUNT,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ids;
    use core::ffi::CStr;

    fn lookup_cstr(s: &CStr) -> SignalId {
        unsafe { signal_id_from_name_rs(s.as_ptr()) }
    }

    #[test]
    fn known_name_resolves() {
        let s = CStr::from_bytes_with_nul(b"rpm\0").unwrap();
        assert_eq!(lookup_cstr(s), ids::RPM);
    }

    #[test]
    fn null_returns_sentinel() {
        let id = unsafe { signal_id_from_name_rs(core::ptr::null()) };
        assert_eq!(id, SIGNAL_COUNT);
    }

    #[test]
    fn empty_returns_sentinel() {
        let s = CStr::from_bytes_with_nul(b"\0").unwrap();
        assert_eq!(lookup_cstr(s), SIGNAL_COUNT);
    }

    #[test]
    fn unknown_returns_sentinel() {
        let s = CStr::from_bytes_with_nul(b"not_a_signal\0").unwrap();
        assert_eq!(lookup_cstr(s), SIGNAL_COUNT);
    }
}
