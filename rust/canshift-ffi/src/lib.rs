#![cfg_attr(not(feature = "std"), no_std)]

#[cfg(feature = "alert-engine")]
extern crate alert_engine;
#[cfg(feature = "ble-link")]
extern crate ble_link;
#[cfg(feature = "bus-scan")]
extern crate bus_scan;
#[cfg(feature = "can-parser")]
extern crate can_parser;
#[cfg(feature = "config-loader")]
extern crate config_loader;
#[cfg(feature = "control-state")]
extern crate control_state;
#[cfg(feature = "error-store")]
extern crate error_store;
#[cfg(feature = "format-float")]
extern crate format_float;
#[cfg(feature = "imu-core")]
extern crate imu_core;
#[cfg(feature = "layout-grid")]
extern crate layout_grid;
#[cfg(feature = "ota-signature")]
extern crate ota_signature;
#[cfg(feature = "signal-map")]
extern crate signal_map;
#[cfg(feature = "timer-core")]
extern crate timer_core;
#[cfg(feature = "tone-sequencer")]
extern crate tone_sequencer;
#[cfg(feature = "unit-convert")]
extern crate unit_convert;
#[cfg(feature = "usb-envelope")]
extern crate usb_envelope;
#[cfg(feature = "workshop-mode")]
extern crate workshop_mode;

#[cfg(not(feature = "std"))]
extern "C" {
    fn abort() -> !;
}

#[cfg(not(feature = "std"))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe { abort() }
}
