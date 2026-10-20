#![allow(clippy::missing_safety_doc)]

use crate::{encode, offer, reset, set_filter, CanMonitor};

unsafe fn bytes<'a>(ptr: *const u8, len: usize) -> Option<&'a [u8]> {
    if len == 0 {
        return Some(&[]);
    }
    if ptr.is_null() {
        return None;
    }
    Some(unsafe { core::slice::from_raw_parts(ptr, len) })
}

#[no_mangle]
pub unsafe extern "C" fn can_monitor_reset_rs(monitor: *mut CanMonitor) {
    if let Some(monitor) = unsafe { monitor.as_mut() } {
        reset(monitor);
    }
}

#[no_mangle]
pub unsafe extern "C" fn can_monitor_offer_rs(
    monitor: *mut CanMonitor,
    id: u32,
    data: *const u8,
    len: u8,
    now_ms: u32,
) {
    let (Some(monitor), Some(data)) = (unsafe { monitor.as_mut() }, unsafe {
        bytes(data, usize::from(len))
    }) else {
        return;
    };
    offer(monitor, id, data, now_ms);
}

#[no_mangle]
pub unsafe extern "C" fn can_monitor_set_filter_rs(
    monitor: *mut CanMonitor,
    data: *const u8,
    len: usize,
) -> bool {
    let (Some(monitor), Some(data)) = (unsafe { monitor.as_mut() }, unsafe { bytes(data, len) })
    else {
        return false;
    };
    set_filter(monitor, data)
}

#[no_mangle]
pub unsafe extern "C" fn can_monitor_encode_rs(
    monitor: *mut CanMonitor,
    now_ms: u32,
    out: *mut u8,
    cap: usize,
) -> usize {
    let Some(monitor) = (unsafe { monitor.as_mut() }) else {
        return 0;
    };
    if out.is_null() || cap == 0 {
        return 0;
    }
    encode(monitor, now_ms, unsafe {
        core::slice::from_raw_parts_mut(out, cap)
    })
}
