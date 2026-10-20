#![allow(clippy::missing_safety_doc)]

use crate::lap_watch::{observe, rebase, LapWatch};
use crate::record::{
    encode_end, encode_header, encode_lap, encode_preamble, encode_sample, DATALOG_SIGNAL_SLOTS,
};
use crate::ring::Ring;
use crate::session::{halt, step, Session, SessionInputs, DATALOG_ACTION_NONE};

unsafe fn out_slice<'a>(out: *mut u8, capacity: usize) -> Option<&'a mut [u8]> {
    if out.is_null() {
        return None;
    }
    Some(unsafe { core::slice::from_raw_parts_mut(out, capacity) })
}

#[no_mangle]
pub unsafe extern "C" fn datalog_preamble_rs(out: *mut u8, capacity: usize) -> usize {
    unsafe { out_slice(out, capacity) }.map_or(0, encode_preamble)
}

#[no_mangle]
pub unsafe extern "C" fn datalog_header_rs(
    out: *mut u8,
    capacity: usize,
    sample_period_ms: u16,
    session: u16,
) -> usize {
    unsafe { out_slice(out, capacity) }
        .map_or(0, |out| encode_header(out, sample_period_ms, session))
}

#[no_mangle]
pub unsafe extern "C" fn datalog_sample_rs(
    out: *mut u8,
    capacity: usize,
    t_ms: u32,
    mask: u64,
    values: *const f32,
) -> usize {
    let Some(values) = (unsafe { values.cast::<[f32; DATALOG_SIGNAL_SLOTS]>().as_ref() }) else {
        return 0;
    };
    unsafe { out_slice(out, capacity) }.map_or(0, |out| encode_sample(out, t_ms, mask, values))
}

#[no_mangle]
pub unsafe extern "C" fn datalog_lap_rs(
    out: *mut u8,
    capacity: usize,
    t_ms: u32,
    index: u16,
    lap_ms: u32,
) -> usize {
    unsafe { out_slice(out, capacity) }.map_or(0, |out| encode_lap(out, t_ms, index, lap_ms))
}

#[no_mangle]
pub unsafe extern "C" fn datalog_end_rs(
    out: *mut u8,
    capacity: usize,
    t_ms: u32,
    dropped: u32,
) -> usize {
    unsafe { out_slice(out, capacity) }.map_or(0, |out| encode_end(out, t_ms, dropped))
}

#[no_mangle]
pub unsafe extern "C" fn datalog_ring_init_rs(
    ring: *mut Ring,
    storage: *mut u8,
    capacity: u32,
) -> bool {
    let Some(made) = (unsafe { Ring::new(storage, capacity) }) else {
        return false;
    };
    if ring.is_null() {
        return false;
    }
    unsafe { ring.write(made) };
    true
}

#[no_mangle]
pub unsafe extern "C" fn datalog_ring_push_rs(
    ring: *const Ring,
    bytes: *const u8,
    len: usize,
) -> bool {
    let Some(ring) = (unsafe { ring.as_ref() }) else {
        return false;
    };
    if bytes.is_null() {
        return false;
    }
    ring.push(unsafe { core::slice::from_raw_parts(bytes, len) })
}

#[no_mangle]
pub unsafe extern "C" fn datalog_ring_pop_rs(
    ring: *const Ring,
    out: *mut u8,
    capacity: usize,
) -> usize {
    let Some(ring) = (unsafe { ring.as_ref() }) else {
        return 0;
    };
    unsafe { out_slice(out, capacity) }.map_or(0, |out| ring.pop_into(out))
}

#[no_mangle]
pub unsafe extern "C" fn datalog_ring_used_rs(ring: *const Ring) -> u32 {
    unsafe { ring.as_ref() }.map_or(0, Ring::used)
}

#[no_mangle]
pub unsafe extern "C" fn datalog_ring_dropped_rs(ring: *const Ring) -> u32 {
    unsafe { ring.as_ref() }.map_or(0, Ring::dropped)
}

#[no_mangle]
pub unsafe extern "C" fn datalog_session_step_rs(
    session: *mut Session,
    inputs: *const SessionInputs,
) -> u8 {
    let (Some(session), Some(inputs)) = (unsafe { session.as_mut() }, unsafe { inputs.as_ref() })
    else {
        return DATALOG_ACTION_NONE;
    };
    step(session, inputs)
}

#[no_mangle]
pub unsafe extern "C" fn datalog_session_halt_rs(session: *mut Session) {
    if let Some(session) = unsafe { session.as_mut() } {
        halt(session);
    }
}

#[no_mangle]
pub unsafe extern "C" fn datalog_lap_observe_rs(
    watch: *mut LapWatch,
    lap_count: u16,
    timer_session: u16,
) -> bool {
    unsafe { watch.as_mut() }.is_some_and(|watch| observe(watch, lap_count, timer_session))
}

#[no_mangle]
pub unsafe extern "C" fn datalog_lap_rebase_rs(
    watch: *mut LapWatch,
    lap_count: u16,
    timer_session: u16,
) {
    if let Some(watch) = unsafe { watch.as_mut() } {
        rebase(watch, lap_count, timer_session);
    }
}
