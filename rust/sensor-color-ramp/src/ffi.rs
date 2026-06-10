use core::ffi::c_char;
use core::slice;

use crate::{
    color_at_value, resolve_ramp, sensor_kind_from_name, ColorRamp, RampStop, SensorKind,
    MAX_RAMP_STOPS,
};

// Compile-time layout guard against C++ CfgColorRampDef drift.
const _: () = assert!(core::mem::size_of::<RampStop>() == 8);
const _: () = assert!(core::mem::size_of::<ColorRamp>() == 4 + MAX_RAMP_STOPS * 8);

const MAX_NAME_LEN: usize = 64;

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

pub const SENSOR_KIND_UNKNOWN_RS: u8 = SensorKind::Unknown as u8;

/// # Safety: `name` null or NUL-terminated. Capped at MAX_NAME_LEN.
#[no_mangle]
pub unsafe extern "C" fn sensor_kind_from_name_rs(name: *const c_char) -> u8 {
    let slice = unsafe { c_str_slice(name) };
    sensor_kind_from_name(slice) as u8
}

/// # Safety: `ramp` valid CfgColorRampDef (or null → 0).
#[no_mangle]
pub unsafe extern "C" fn color_at_value_rs(ramp: *const ColorRamp, value: f32) -> u32 {
    if ramp.is_null() {
        return 0x000000;
    }
    color_at_value(unsafe { &*ramp }, value)
}

/// per_signal wins when count > 0; else falls through to default catalog.
/// # Safety: `per_signal` valid; `signal_name` null or NUL-terminated.
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
            assert_eq!((*p).count, 4);
            assert_eq!((*p).stops[0].color, 0x4A90E2);
        }
    }
}
