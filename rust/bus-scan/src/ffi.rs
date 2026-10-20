#![allow(clippy::missing_safety_doc)]

use crate::scan::{abort, begin, fail, on_frame, queue, tick, BusScan, BUS_SCAN_ACTION_NONE};

#[no_mangle]
pub unsafe extern "C" fn bus_scan_begin_rs(scan: *mut BusScan, now_ms: u32) -> u8 {
    let Some(scan) = (unsafe { scan.as_mut() }) else {
        return BUS_SCAN_ACTION_NONE;
    };
    begin(scan, now_ms)
}

#[no_mangle]
pub unsafe extern "C" fn bus_scan_on_frame_rs(scan: *mut BusScan, id: u32) {
    if let Some(scan) = unsafe { scan.as_mut() } {
        on_frame(scan, id);
    }
}

#[no_mangle]
pub unsafe extern "C" fn bus_scan_tick_rs(scan: *mut BusScan, now_ms: u32, bus_errors: u32) -> u8 {
    let Some(scan) = (unsafe { scan.as_mut() }) else {
        return BUS_SCAN_ACTION_NONE;
    };
    tick(scan, now_ms, bus_errors)
}

#[no_mangle]
pub unsafe extern "C" fn bus_scan_fail_rs(scan: *mut BusScan) -> u8 {
    let Some(scan) = (unsafe { scan.as_mut() }) else {
        return BUS_SCAN_ACTION_NONE;
    };
    fail(scan)
}

#[no_mangle]
pub unsafe extern "C" fn bus_scan_abort_rs(scan: *mut BusScan) {
    if let Some(scan) = unsafe { scan.as_mut() } {
        abort(scan);
    }
}

#[no_mangle]
pub unsafe extern "C" fn bus_scan_queue_rs(scan: *mut BusScan) {
    if let Some(scan) = unsafe { scan.as_mut() } {
        queue(scan);
    }
}
