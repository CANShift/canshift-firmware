use crate::{TimerCore, TimerLap, LAP_CAPACITY};

const _: () = assert!(core::mem::size_of::<TimerLap>() == 12);
const _: () = assert!(core::mem::size_of::<TimerCore>() == 40 + LAP_CAPACITY * 12);

#[no_mangle]
pub unsafe extern "C" fn timer_core_init_rs(core: *mut TimerCore) {
    if core.is_null() {
        return;
    }
    unsafe { &mut *core }.init();
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_start_rs(core: *mut TimerCore, now_us: i64) -> bool {
    if core.is_null() {
        return false;
    }
    unsafe { &mut *core }.start(now_us)
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_pause_rs(core: *mut TimerCore, now_us: i64) -> bool {
    if core.is_null() {
        return false;
    }
    unsafe { &mut *core }.pause(now_us)
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_resume_rs(core: *mut TimerCore, now_us: i64) -> bool {
    if core.is_null() {
        return false;
    }
    unsafe { &mut *core }.resume(now_us)
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_reset_rs(core: *mut TimerCore) -> bool {
    if core.is_null() {
        return false;
    }
    unsafe { &mut *core }.reset()
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_lap_rs(
    core: *mut TimerCore,
    now_us: i64,
    out_lap: *mut TimerLap,
    out_dropped_oldest: *mut bool,
) -> bool {
    if core.is_null() {
        return false;
    }
    match unsafe { &mut *core }.lap(now_us) {
        Some(outcome) => {
            if !out_lap.is_null() {
                unsafe { *out_lap = outcome.lap };
            }
            if !out_dropped_oldest.is_null() {
                unsafe { *out_dropped_oldest = outcome.dropped_oldest };
            }
            true
        }
        None => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_elapsed_ms_rs(core: *const TimerCore, now_us: i64) -> u32 {
    if core.is_null() {
        return 0;
    }
    unsafe { &*core }.elapsed_ms(now_us)
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_pending_count_rs(core: *const TimerCore) -> u8 {
    if core.is_null() {
        return 0;
    }
    unsafe { &*core }.pending_count()
}

#[no_mangle]
pub unsafe extern "C" fn timer_core_pop_pending_rs(
    core: *mut TimerCore,
    out_lap: *mut TimerLap,
) -> bool {
    if core.is_null() || out_lap.is_null() {
        return false;
    }
    match unsafe { &mut *core }.pop_pending() {
        Some(lap) => {
            unsafe { *out_lap = lap };
            true
        }
        None => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SEC: i64 = 1_000_000;

    #[test]
    fn ffi_full_cycle() {
        let mut core = TimerCore::new();
        unsafe {
            timer_core_init_rs(&mut core);
            assert!(timer_core_start_rs(&mut core, 0));
            assert_eq!(timer_core_elapsed_ms_rs(&core, 2 * SEC), 2_000);

            let mut lap = TimerLap::default();
            let mut dropped = false;
            assert!(timer_core_lap_rs(
                &mut core,
                3 * SEC,
                &mut lap,
                &mut dropped
            ));
            assert_eq!(lap.index, 1);
            assert_eq!(lap.lap_ms, 3_000);
            assert!(!dropped);

            assert!(timer_core_pause_rs(&mut core, 4 * SEC));
            assert!(timer_core_resume_rs(&mut core, 10 * SEC));
            assert!(timer_core_reset_rs(&mut core));

            assert_eq!(timer_core_pending_count_rs(&core), 1);
            let mut popped = TimerLap::default();
            assert!(timer_core_pop_pending_rs(&mut core, &mut popped));
            assert_eq!(popped.index, 1);
            assert!(!timer_core_pop_pending_rs(&mut core, &mut popped));
        }
    }

    #[test]
    fn ffi_null_pointers_are_noops() {
        unsafe {
            timer_core_init_rs(core::ptr::null_mut());
            assert!(!timer_core_start_rs(core::ptr::null_mut(), 0));
            assert!(!timer_core_reset_rs(core::ptr::null_mut()));
            assert_eq!(timer_core_elapsed_ms_rs(core::ptr::null(), SEC), 0);
            assert_eq!(timer_core_pending_count_rs(core::ptr::null()), 0);

            let mut core_state = TimerCore::new();
            timer_core_start_rs(&mut core_state, 0);
            assert!(timer_core_lap_rs(
                &mut core_state,
                SEC,
                core::ptr::null_mut(),
                core::ptr::null_mut()
            ));
            assert!(!timer_core_pop_pending_rs(
                &mut core_state,
                core::ptr::null_mut()
            ));
        }
    }
}
