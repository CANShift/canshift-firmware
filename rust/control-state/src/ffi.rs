use crate::{resolve, step_tap, Stepper};

const _: () = assert!(core::mem::size_of::<Stepper>() == 8);

#[no_mangle]
pub extern "C" fn control_state_resolve_rs(blocked: bool, acting: bool, requested: bool) -> u8 {
    resolve(blocked, acting, requested).as_u8()
}

#[no_mangle]
pub extern "C" fn control_step_tap_rs(level: u8) -> u8 {
    step_tap(level)
}

#[no_mangle]
pub unsafe extern "C" fn control_stepper_init_rs(stepper: *mut Stepper, level: u8) {
    if stepper.is_null() {
        return;
    }
    unsafe { *stepper = Stepper::new(level) };
}

#[no_mangle]
pub unsafe extern "C" fn control_stepper_press_rs(stepper: *mut Stepper, now_ms: u32) {
    if stepper.is_null() {
        return;
    }
    unsafe { &mut *stepper }.press(now_ms);
}

#[no_mangle]
pub unsafe extern "C" fn control_stepper_poll_rs(stepper: *mut Stepper, now_ms: u32) -> bool {
    if stepper.is_null() {
        return false;
    }
    unsafe { &mut *stepper }.poll(now_ms)
}

#[no_mangle]
pub unsafe extern "C" fn control_stepper_release_rs(stepper: *mut Stepper, now_ms: u32) -> bool {
    if stepper.is_null() {
        return false;
    }
    unsafe { &mut *stepper }.release(now_ms)
}

#[no_mangle]
pub unsafe extern "C" fn control_stepper_sync_rs(stepper: *mut Stepper, level: u8) -> bool {
    if stepper.is_null() {
        return false;
    }
    unsafe { &mut *stepper }.sync(level)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{ControlState, CONTROL_LONG_PRESS_MS, CONTROL_STEP_MAX};

    #[test]
    fn ffi_resolve_matches_the_enum() {
        assert_eq!(
            control_state_resolve_rs(true, false, false),
            ControlState::Unavailable.as_u8()
        );
        assert_eq!(
            control_state_resolve_rs(false, true, false),
            ControlState::Active.as_u8()
        );
        assert_eq!(
            control_state_resolve_rs(false, false, true),
            ControlState::Armed.as_u8()
        );
        assert_eq!(
            control_state_resolve_rs(false, false, false),
            ControlState::Off.as_u8()
        );
    }

    #[test]
    fn ffi_tap_wraps_past_the_top() {
        assert_eq!(control_step_tap_rs(CONTROL_STEP_MAX), 0);
        assert_eq!(control_step_tap_rs(0), 1);
    }

    #[test]
    fn ffi_stepper_tap_then_long_press() {
        let mut s = Stepper::default();
        unsafe {
            control_stepper_init_rs(&mut s, 5);
            control_stepper_press_rs(&mut s, 0);
            assert!(control_stepper_release_rs(&mut s, 100));
            assert_eq!(s.level, CONTROL_STEP_MAX);

            control_stepper_press_rs(&mut s, 1_000);
            assert!(control_stepper_poll_rs(
                &mut s,
                1_000 + CONTROL_LONG_PRESS_MS
            ));
            assert_eq!(s.level, 1);
            assert!(!control_stepper_release_rs(&mut s, 2_000));
            assert!(control_stepper_sync_rs(&mut s, 3));
            assert_eq!(s.level, 3);
        }
    }

    #[test]
    fn ffi_null_pointers_are_noops() {
        unsafe {
            control_stepper_init_rs(core::ptr::null_mut(), 2);
            control_stepper_press_rs(core::ptr::null_mut(), 0);
            assert!(!control_stepper_poll_rs(core::ptr::null_mut(), 5_000));
            assert!(!control_stepper_release_rs(core::ptr::null_mut(), 5_000));
            assert!(!control_stepper_sync_rs(core::ptr::null_mut(), 4));
        }
    }
}
