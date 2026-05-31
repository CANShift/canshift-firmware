// ffi.rs — C ABI shim for the sensor-color-ramp crate. Surface matches the
// existing `ui/sensor_color_ramp.h` so the firmware callers (gauge / bar /
// widget renderers) keep working unchanged when `USE_RUST_SENSOR_COLOR_RAMP=1`
// is set.
//
// The `ColorRamp` / `RampStop` repr(C) layouts are byte-for-byte identical
// to `CfgColorRampDef` / `CfgRampStopDef` in `config_types.h` — see the
// compile-time `assert!` blocks below for the guard.

use core::ffi::c_char;
use core::slice;

use crate::{
    color_at_value, resolve_ramp, sensor_kind_from_name, ColorRamp, RampStop, SensorKind,
    MAX_RAMP_STOPS,
};

// Compile-time guard — drift between the C++ `CfgColorRampDef` layout and
// the Rust `ColorRamp` mirror would silently corrupt the gauge color path.
// Bump CFG_MAX_RAMP_STOPS or change CfgRampStopDef and this fires at crate
// build time.
const _: () = assert!(core::mem::size_of::<RampStop>() == 8);
const _: () = assert!(core::mem::size_of::<ColorRamp>() == 4 + MAX_RAMP_STOPS * 8);

// Defensive cap on the signal-name scan — config_loader keeps names under
// `CFG_MAX_SIGNAL_LEN`, but a hostile / corrupted JSON could feed a longer
// string. The cap is generous (twice the firmware limit).
const MAX_NAME_LEN: usize = 64;

/// Read up to `MAX_NAME_LEN` bytes from a C string, capped at the first NUL.
/// Returns an empty slice when `ptr` is null.
unsafe fn c_str_slice<'a>(ptr: *const c_char) -> &'a [u8] {
    if ptr.is_null() {
        return &[];
    }
    let mut len = 0usize;
    while len < MAX_NAME_LEN {
        if unsafe { *(ptr as *const u8).add(len) } == 0 {
            break;
        }
        len += 1;
    }
    unsafe { slice::from_raw_parts(ptr as *const u8, len) }
}

/// `SensorKind::Unknown` discriminant for the C side — mirrors the
/// `SensorKind::Unknown = 9` constant in `ui/sensor_color_ramp.h`.
pub const SENSOR_KIND_UNKNOWN_RS: u8 = SensorKind::Unknown as u8;

/// Resolve a free-form signal name to a `SensorKind` discriminant. Returns
/// `SENSOR_KIND_UNKNOWN_RS` (9) on null / empty / no-match inputs. Caller
/// casts to the C++ `SensorKind` enum.
///
/// # Safety
/// `name` must be null or a NUL-terminated C string whose readable length is
/// at most `MAX_NAME_LEN`. Strings longer than the cap are treated as if
/// truncated at the cap.
#[no_mangle]
pub unsafe extern "C" fn sensor_kind_from_name_rs(name: *const c_char) -> u8 {
    let slice = unsafe { c_str_slice(name) };
    sensor_kind_from_name(slice) as u8
}

/// Sample the ramp at `value`. Returns `0x00RRGGBB`. See `color_at_value` in
/// lib.rs for the contract.
///
/// # Safety
/// `ramp` must point to a valid `CfgColorRampDef` (== `ColorRamp` after the
/// layout asserts above).
#[no_mangle]
pub unsafe extern "C" fn color_at_value_rs(ramp: *const ColorRamp, value: f32) -> u32 {
    if ramp.is_null() {
        return 0x000000;
    }
    color_at_value(unsafe { &*ramp }, value)
}

/// Resolve the active ramp for a widget. Mirrors the C++ `resolveRamp`
/// surface: returns a pointer into either `per_signal` (when its `count > 0`)
/// or the static default catalog (when the name resolves), or null when
/// neither path applies.
///
/// # Safety
/// `per_signal` must point to a valid `CfgColorRampDef`. `signal_name` may
/// be null or a NUL-terminated C string (same rules as
/// `sensor_kind_from_name_rs`).
#[no_mangle]
pub unsafe extern "C" fn resolve_ramp_rs(
    per_signal: *const ColorRamp,
    signal_name: *const c_char,
) -> *const ColorRamp {
    if per_signal.is_null() {
        return core::ptr::null();
    }
    let name_slice = unsafe { c_str_slice(signal_name) };
    match resolve_ramp(unsafe { &*per_signal }, name_slice) {
        Some(r) => r as *const ColorRamp,
        None => core::ptr::null(),
    }
}

/// Pointer to the static default catalog. The firmware-side
/// `kSensorDefaultRamps` array is the C++ mirror; both must match
/// byte-for-byte. Exposing the pointer lets the FFI assertion verify
/// parity at boot.
#[no_mangle]
pub extern "C" fn sensor_default_ramps_rs() -> *const ColorRamp {
    crate::SENSOR_DEFAULT_RAMPS.as_ptr()
}

/// Count of named kinds in the default catalog — matches
/// `kSensorKindCount` in the C++ header.
#[no_mangle]
pub extern "C" fn sensor_kind_count_rs() -> u8 {
    crate::SENSOR_KIND_COUNT as u8
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cstr(s: &[u8]) -> Vec<u8> {
        let mut v = s.to_vec();
        v.push(0);
        v
    }

    #[test]
    fn ffi_sensor_kind_from_name() {
        let s = cstr(b"coolant_temp_c");
        unsafe {
            assert_eq!(
                sensor_kind_from_name_rs(s.as_ptr() as *const c_char),
                SensorKind::Coolant as u8
            );
        }
    }

    #[test]
    fn ffi_sensor_kind_null_returns_unknown() {
        unsafe {
            assert_eq!(sensor_kind_from_name_rs(core::ptr::null()), SENSOR_KIND_UNKNOWN_RS);
        }
    }

    #[test]
    fn ffi_color_at_value_null_returns_black() {
        unsafe {
            assert_eq!(color_at_value_rs(core::ptr::null(), 0.0), 0x000000);
        }
    }

    #[test]
    fn ffi_color_at_value_with_catalog() {
        let r = &crate::SENSOR_DEFAULT_RAMPS[SensorKind::Coolant as usize];
        unsafe {
            assert_eq!(color_at_value_rs(r as *const ColorRamp, 30.0), 0x4A90E2);
        }
    }

    #[test]
    fn ffi_resolve_ramp_uses_default_when_per_signal_empty() {
        let per = ColorRamp {
            count: 0,
            interpolate: crate::RampInterp::Linear,
            _padding: [0; 2],
            stops: [RampStop { value: 0.0, color: 0 }; MAX_RAMP_STOPS],
        };
        let name = cstr(b"coolant_temp_c");
        unsafe {
            let p = resolve_ramp_rs(&per as *const ColorRamp, name.as_ptr() as *const c_char);
            assert!(!p.is_null());
            assert_eq!((*p).count, 4);
            assert_eq!((*p).stops[0].color, 0x4A90E2);
        }
    }

    #[test]
    fn ffi_resolve_ramp_null_per_signal_returns_null() {
        let name = cstr(b"coolant_temp_c");
        unsafe {
            let p = resolve_ramp_rs(core::ptr::null(), name.as_ptr() as *const c_char);
            assert!(p.is_null());
        }
    }

    #[test]
    fn ffi_catalog_pointer_and_count_round_trip() {
        let p = sensor_default_ramps_rs();
        let n = sensor_kind_count_rs();
        assert!(!p.is_null());
        assert_eq!(n, 9);
        unsafe {
            assert_eq!((*p).count, 4); // Coolant — 4 stops
            assert_eq!((*p).stops[0].color, 0x4A90E2);
        }
    }
}
