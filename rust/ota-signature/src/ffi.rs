use core::mem::{align_of, size_of};
use core::{ptr, slice};

use crate::{
    OtaSignature, OTA_SIGNATURE_BYTES, OTA_SIGNATURE_STATE_ALIGN, OTA_SIGNATURE_STATE_BYTES,
};

#[repr(C, align(16))]
pub struct OtaSignatureSlot {
    bytes: [u8; OTA_SIGNATURE_STATE_BYTES],
}

type Session = Option<OtaSignature>;

const _: () = assert!(size_of::<Session>() <= OTA_SIGNATURE_STATE_BYTES);
const _: () = assert!(align_of::<Session>() <= align_of::<OtaSignatureSlot>());
const _: () = assert!(align_of::<OtaSignatureSlot>() == OTA_SIGNATURE_STATE_ALIGN);

fn session(slot: *mut OtaSignatureSlot) -> *mut Session {
    slot.cast::<Session>()
}

#[no_mangle]
pub unsafe extern "C" fn ota_signature_clear_rs(slot: *mut OtaSignatureSlot) {
    if slot.is_null() {
        return;
    }
    unsafe { ptr::write(session(slot), None) };
}

#[no_mangle]
pub unsafe extern "C" fn ota_signature_begin_rs(
    slot: *mut OtaSignatureSlot,
    signature: *const u8,
) -> bool {
    if slot.is_null() {
        return false;
    }
    let started = if signature.is_null() {
        None
    } else {
        let bytes = unsafe { &*signature.cast::<[u8; OTA_SIGNATURE_BYTES]>() };
        OtaSignature::begin_release(bytes)
    };
    let ok = started.is_some();
    unsafe { ptr::write(session(slot), started) };
    ok
}

#[no_mangle]
pub unsafe extern "C" fn ota_signature_update_rs(
    slot: *mut OtaSignatureSlot,
    data: *const u8,
    len: usize,
) {
    if slot.is_null() || data.is_null() {
        return;
    }
    let chunk = unsafe { slice::from_raw_parts(data, len) };
    if let Some(check) = unsafe { &mut *session(slot) } {
        check.update(chunk);
    }
}

#[no_mangle]
pub unsafe extern "C" fn ota_signature_finish_rs(slot: *mut OtaSignatureSlot) -> bool {
    if slot.is_null() {
        return false;
    }
    let taken = unsafe { ptr::replace(session(slot), None) };
    taken.is_some_and(OtaSignature::finish)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cleared() -> OtaSignatureSlot {
        let mut slot = OtaSignatureSlot {
            bytes: [0; OTA_SIGNATURE_STATE_BYTES],
        };
        unsafe { ota_signature_clear_rs(&mut slot) };
        slot
    }

    #[test]
    fn a_cleared_slot_never_verifies() {
        let mut slot = cleared();
        unsafe {
            ota_signature_update_rs(&mut slot, [1u8, 2, 3].as_ptr(), 3);
            assert!(!ota_signature_finish_rs(&mut slot));
        }
    }

    #[test]
    fn a_missing_signature_does_not_begin() {
        let mut slot = cleared();
        unsafe {
            assert!(!ota_signature_begin_rs(&mut slot, ptr::null()));
            assert!(!ota_signature_finish_rs(&mut slot));
        }
    }

    #[test]
    fn a_foreign_signature_begins_but_fails_on_the_release_key() {
        let mut slot = cleared();
        let signature = [0x11u8; OTA_SIGNATURE_BYTES];
        let mut canonical = signature;
        canonical[63] = 0x00;
        unsafe {
            assert!(ota_signature_begin_rs(&mut slot, canonical.as_ptr()));
            ota_signature_update_rs(&mut slot, b"image".as_ptr(), 5);
            assert!(!ota_signature_finish_rs(&mut slot));
            assert!(!ota_signature_finish_rs(&mut slot));
        }
    }

    #[test]
    fn null_slots_are_ignored() {
        unsafe {
            ota_signature_clear_rs(ptr::null_mut());
            ota_signature_update_rs(ptr::null_mut(), ptr::null(), 0);
            assert!(!ota_signature_begin_rs(ptr::null_mut(), ptr::null()));
            assert!(!ota_signature_finish_rs(ptr::null_mut()));
        }
    }
}
