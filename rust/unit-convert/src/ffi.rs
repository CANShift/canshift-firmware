use core::ffi::{c_char, CStr};
use core::ptr;

use crate::{display_symbol, display_value, UnitSystem};

// # Safety: `unit` null or NUL-terminated. The returned pointer is static.
#[no_mangle]
pub unsafe extern "C" fn unit_display_symbol_rs(unit: *const c_char, system: u8) -> *const c_char {
    if unit.is_null() {
        return ptr::null();
    }
    let bytes = unsafe { CStr::from_ptr(unit) }.to_bytes();
    match display_symbol(bytes, UnitSystem::from_u8(system)) {
        Some(symbol) => symbol.as_ptr() as *const c_char,
        None => ptr::null(),
    }
}

// # Safety: `unit` null or NUL-terminated.
#[no_mangle]
pub unsafe extern "C" fn unit_display_value_rs(value: f32, unit: *const c_char, system: u8) -> f32 {
    if unit.is_null() {
        return value;
    }
    let bytes = unsafe { CStr::from_ptr(unit) }.to_bytes();
    display_value(value, bytes, UnitSystem::from_u8(system))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{UNIT_SYSTEM_IMPERIAL, UNIT_SYSTEM_METRIC};

    fn as_str(raw: *const c_char) -> Option<&'static str> {
        if raw.is_null() {
            return None;
        }
        Some(unsafe { CStr::from_ptr(raw) }.to_str().unwrap())
    }

    #[test]
    fn ffi_symbol_swaps_for_imperial() {
        let unit = b"km/h\0";
        let raw =
            unsafe { unit_display_symbol_rs(unit.as_ptr() as *const c_char, UNIT_SYSTEM_IMPERIAL) };
        assert_eq!(as_str(raw), Some("mph"));
    }

    #[test]
    fn ffi_symbol_is_null_when_the_unit_has_no_pair() {
        let unit = b"rpm\0";
        let raw =
            unsafe { unit_display_symbol_rs(unit.as_ptr() as *const c_char, UNIT_SYSTEM_IMPERIAL) };
        assert_eq!(as_str(raw), None);
    }

    #[test]
    fn ffi_symbol_null_unit_is_null() {
        assert!(unsafe { unit_display_symbol_rs(ptr::null(), UNIT_SYSTEM_IMPERIAL) }.is_null());
    }

    #[test]
    fn ffi_value_converts_and_leaves_metric_alone() {
        let unit = b"\xc2\xb0C\0";
        let hot = unsafe {
            unit_display_value_rs(100.0, unit.as_ptr() as *const c_char, UNIT_SYSTEM_IMPERIAL)
        };
        assert!((hot - 212.0).abs() < 0.001);
        let same = unsafe {
            unit_display_value_rs(100.0, unit.as_ptr() as *const c_char, UNIT_SYSTEM_METRIC)
        };
        assert_eq!(same, 100.0);
    }

    #[test]
    fn ffi_value_null_unit_passes_through() {
        assert_eq!(
            unsafe { unit_display_value_rs(9.5, ptr::null(), UNIT_SYSTEM_IMPERIAL) },
            9.5
        );
    }
}
