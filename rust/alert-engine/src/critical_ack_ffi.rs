use crate::critical_ack::{acknowledge, first_unacknowledged, hold, retain};

#[no_mangle]
pub extern "C" fn alert_critical_acknowledge_rs(acknowledged: u32, source: i8) -> u32 {
    acknowledge(acknowledged, source)
}

#[no_mangle]
pub extern "C" fn alert_critical_retain_rs(acknowledged: u32, critical: u32) -> u32 {
    retain(acknowledged, critical)
}

#[no_mangle]
pub extern "C" fn alert_critical_first_unacknowledged_rs(critical: u32, acknowledged: u32) -> i8 {
    first_unacknowledged(critical, acknowledged)
}

#[no_mangle]
pub extern "C" fn alert_critical_hold_rs(
    shown: u32,
    critical: u32,
    acknowledged: u32,
    no_data: u32,
) -> u32 {
    hold(shown, critical, acknowledged, no_data)
}
