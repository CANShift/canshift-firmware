use crate::cuts::{kind_name, rows, step, CutBandState, CutRow, CUT_ROW_CAPACITY};

const _: () = assert!(core::mem::size_of::<CutRow>() == 8);

#[no_mangle]
pub extern "C" fn cut_band_reset_rs(state: *mut CutBandState) {
    if state.is_null() {
        return;
    }
    unsafe { *state = CutBandState::default() };
}

#[no_mangle]
pub extern "C" fn cut_band_step_rs(state: *mut CutBandState, flags: u16, now_ms: u32) {
    if state.is_null() {
        return;
    }
    step(unsafe { &mut *state }, flags, now_ms);
}

#[no_mangle]
pub extern "C" fn cut_band_rows_rs(
    state: *const CutBandState,
    now_ms: u32,
    out: *mut CutRow,
) -> u8 {
    if state.is_null() || out.is_null() {
        return 0;
    }
    let slots = unsafe { &mut *(out as *mut [CutRow; CUT_ROW_CAPACITY]) };
    rows(unsafe { &*state }, now_ms, slots)
}

#[no_mangle]
pub extern "C" fn cut_kind_name_rs(kind: u8) -> *const core::ffi::c_char {
    kind_name(kind).as_ptr() as *const core::ffi::c_char
}
