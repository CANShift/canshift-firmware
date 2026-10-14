use crate::{signal_id_from_name, terminated_name_len, SignalId, SIGNAL_COUNT};
use core::slice;

/// # Safety
/// `name` is null or points to a NUL-terminated string. Bytes are read one at a
/// time up to the NUL, never past it, and never past MAX_NAME_LEN.
#[no_mangle]
pub unsafe extern "C" fn signal_id_from_name_rs(name: *const core::ffi::c_char) -> SignalId {
    if name.is_null() {
        return SIGNAL_COUNT;
    }
    let bytes = name.cast::<u8>();
    let Some(len) = terminated_name_len(|i| unsafe { *bytes.add(i) }) else {
        return SIGNAL_COUNT;
    };
    let name = unsafe { slice::from_raw_parts(bytes, len) };
    core::str::from_utf8(name).map_or(SIGNAL_COUNT, signal_id_from_name)
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
        let s = c"rpm";
        assert_eq!(lookup_cstr(s), ids::RPM);
    }

    #[test]
    fn null_returns_sentinel() {
        let id = unsafe { signal_id_from_name_rs(core::ptr::null()) };
        assert_eq!(id, SIGNAL_COUNT);
    }

    #[test]
    fn empty_returns_sentinel() {
        let s = c"";
        assert_eq!(lookup_cstr(s), SIGNAL_COUNT);
    }

    #[test]
    fn short_name_in_an_exact_allocation_resolves() {
        let exact: Box<[u8]> = Box::new(*b"rpm\0");
        let id = unsafe { signal_id_from_name_rs(exact.as_ptr().cast()) };
        assert_eq!(id, ids::RPM);
    }

    #[test]
    fn unterminated_name_is_rejected_at_the_cap() {
        let long = [b'a'; crate::MAX_NAME_LEN + 1];
        let id = unsafe { signal_id_from_name_rs(long.as_ptr().cast()) };
        assert_eq!(id, SIGNAL_COUNT);
    }

    #[test]
    fn unknown_returns_sentinel() {
        let s = c"not_a_signal";
        assert_eq!(lookup_cstr(s), SIGNAL_COUNT);
    }
}
