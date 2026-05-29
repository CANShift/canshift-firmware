//! Rust port of `canshift-firmware/src/can/signal_map.cpp` (issue #1177 R-4).
//!
//! Single source of truth for mapping the string keys used in
//! `dashboard.json` (`CfgWidget.signalId`, `CfgTopBarItem.signalId`) to the
//! numeric `SignalId` values used in `SignalStore`. The C++ original is a
//! 22-entry linear scan over a `constexpr NameToId[]` table; this port keeps
//! the same shape so adding a signal stays a one-line edit on each side.
//!
//! Why R-4 was chosen as the first port post-#1130:
//! - **Smallest meaningful surface** — one function, one FFI symbol, zero
//!   crypto / heap / threading concerns.
//! - **Catches a real bug class** — `strcmp` on a possibly-null pointer or
//!   a non-NUL-terminated buffer is the kind of thing Rust slices reject
//!   at the type level. The C++ original guards `name == nullptr` but not
//!   the missing-NUL case; Rust takes a `&[u8]` + length and that's just
//!   not expressible.
//! - **Drop-in replacement** — same `(const char *) -> uint8_t` ABI as
//!   the C++ function, so `signal_map.cpp` can delegate via the FFI shim
//!   without touching any call site.

#![cfg_attr(not(any(test, feature = "std")), no_std)]

#[cfg(feature = "ffi")]
pub mod ffi;

// Panic handler — required for `no_std + staticlib`. Halts forever. The
// only path that can reach a panic in this crate is an internal invariant
// break (the table is `&'static`, the lookup never indexes out of range),
// so the strategy matches `ota-hmac`'s: spin loop and rely on the C
// caller's error return path (we return `SIGNAL_COUNT` on miss, never
// panic on the happy path).
#[cfg(all(feature = "ffi", not(any(test, feature = "std"))))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// Numeric signal identifier. Mirrors the C++ `SignalId = uint8_t` alias.
pub type SignalId = u8;

/// Sentinel returned for an unknown signal name. Mirrors
/// `SignalIds::SIGNAL_COUNT` in `signal_map.h` — callers must check before
/// indexing into `SignalStore`. Keep in lockstep with the C++ constant.
pub const SIGNAL_COUNT: SignalId = 64;

// ---------------------------------------------------------------------------
// Well-known signal IDs — must match `signal_map.h` exactly (audit C-CS-1).
// ---------------------------------------------------------------------------
//
// Tests in `tests/parity.rs` lock the values down so a drift on either side
// surfaces in CI before it ships. The numeric gaps (15..=19, 25..=29,
// 32..=39, 41..=63) are intentional — they leave room for future signals in
// each semantic band without renumbering.

pub mod ids {
    use super::SignalId;
    // Engine
    pub const RPM: SignalId = 0;
    pub const THROTTLE_POS: SignalId = 1;
    pub const MAP_KPA: SignalId = 2;
    pub const BOOST_BAR: SignalId = 3;
    pub const IAT_C: SignalId = 4;
    // Temperatures
    pub const COOLANT_TEMP_C: SignalId = 5;
    pub const OIL_TEMP_C: SignalId = 6;
    // Pressures
    pub const OIL_PRESS_BAR: SignalId = 7;
    pub const FUEL_PRESS_BAR: SignalId = 8;
    // Fueling
    pub const LAMBDA_1: SignalId = 9;
    pub const AFR_1: SignalId = 10;
    // Vehicle
    pub const SPEED_KPH: SignalId = 11;
    pub const GEAR: SignalId = 12;
    // Electrical
    pub const BATTERY_VOLTS: SignalId = 13;
    // ECU status flags
    pub const FLAG_MIL: SignalId = 20;
    pub const FLAG_LAUNCH_CTRL: SignalId = 21;
    pub const FLAG_FLAT_SHIFT: SignalId = 22;
    pub const FLAG_ANTI_LAG: SignalId = 23;
    pub const FLAG_TRACTION_CUT: SignalId = 24;
    // ECU map/profile info
    pub const MAP_NUMBER: SignalId = 30;
    pub const MAP_NAME_IDX: SignalId = 31;
    // Lap timer
    pub const LAP_TIMER_MS: SignalId = 40;
}

/// Static name → id table. Linear scan over 22 entries — same shape as the
/// C++ `kNameToId[]`. Linear works fine at this size; switching to a
/// `phf::Map` would buy us nothing the C++ scan doesn't already pay for.
const NAME_TO_ID: &[(&str, SignalId)] = &[
    ("rpm", ids::RPM),
    ("throttle_pos", ids::THROTTLE_POS),
    ("map_kpa", ids::MAP_KPA),
    ("boost_bar", ids::BOOST_BAR),
    ("iat_c", ids::IAT_C),
    ("coolant_temp_c", ids::COOLANT_TEMP_C),
    ("oil_temp_c", ids::OIL_TEMP_C),
    ("oil_press_bar", ids::OIL_PRESS_BAR),
    ("fuel_press_bar", ids::FUEL_PRESS_BAR),
    ("lambda_1", ids::LAMBDA_1),
    ("afr_1", ids::AFR_1),
    ("speed_kph", ids::SPEED_KPH),
    ("gear", ids::GEAR),
    ("battery_volts", ids::BATTERY_VOLTS),
    ("flag_mil", ids::FLAG_MIL),
    ("flag_launch_ctrl", ids::FLAG_LAUNCH_CTRL),
    ("flag_flat_shift", ids::FLAG_FLAT_SHIFT),
    ("flag_anti_lag", ids::FLAG_ANTI_LAG),
    ("flag_traction_cut", ids::FLAG_TRACTION_CUT),
    ("map_number", ids::MAP_NUMBER),
    ("map_name_idx", ids::MAP_NAME_IDX),
    ("lap_timer_ms", ids::LAP_TIMER_MS),
];

/// Resolve a signal name to its `SignalId`. Returns `SIGNAL_COUNT` on
/// unknown name or empty input. Public for `cargo test` parity checks.
#[must_use]
pub fn signal_id_from_name(name: &str) -> SignalId {
    if name.is_empty() {
        return SIGNAL_COUNT;
    }
    for &(candidate, id) in NAME_TO_ID {
        if candidate == name {
            return id;
        }
    }
    SIGNAL_COUNT
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_names_resolve() {
        assert_eq!(signal_id_from_name("rpm"), ids::RPM);
        assert_eq!(signal_id_from_name("coolant_temp_c"), ids::COOLANT_TEMP_C);
        assert_eq!(signal_id_from_name("lap_timer_ms"), ids::LAP_TIMER_MS);
    }

    #[test]
    fn unknown_returns_sentinel() {
        assert_eq!(signal_id_from_name("nope"), SIGNAL_COUNT);
        assert_eq!(signal_id_from_name(""), SIGNAL_COUNT);
        // Mid-string + suffix variants — must not partial-match.
        assert_eq!(signal_id_from_name("rpm_extra"), SIGNAL_COUNT);
        assert_eq!(signal_id_from_name("RPM"), SIGNAL_COUNT); // case-sensitive
    }

    #[test]
    fn table_covers_every_id_in_module() {
        // Every constant we expose has to appear in the lookup table — if
        // someone adds a new SignalId without a name, this catches it.
        let known_ids: &[SignalId] = &[
            ids::RPM,
            ids::THROTTLE_POS,
            ids::MAP_KPA,
            ids::BOOST_BAR,
            ids::IAT_C,
            ids::COOLANT_TEMP_C,
            ids::OIL_TEMP_C,
            ids::OIL_PRESS_BAR,
            ids::FUEL_PRESS_BAR,
            ids::LAMBDA_1,
            ids::AFR_1,
            ids::SPEED_KPH,
            ids::GEAR,
            ids::BATTERY_VOLTS,
            ids::FLAG_MIL,
            ids::FLAG_LAUNCH_CTRL,
            ids::FLAG_FLAT_SHIFT,
            ids::FLAG_ANTI_LAG,
            ids::FLAG_TRACTION_CUT,
            ids::MAP_NUMBER,
            ids::MAP_NAME_IDX,
            ids::LAP_TIMER_MS,
        ];
        for id in known_ids {
            assert!(
                NAME_TO_ID.iter().any(|(_, v)| v == id),
                "SignalId {id} missing from NAME_TO_ID"
            );
        }
        assert_eq!(NAME_TO_ID.len(), known_ids.len());
    }

    #[test]
    fn table_has_no_duplicates() {
        for (i, (name, _)) in NAME_TO_ID.iter().enumerate() {
            for (j, (other, _)) in NAME_TO_ID.iter().enumerate() {
                if i == j {
                    continue;
                }
                assert_ne!(name, other, "duplicate name {name} at indices {i} and {j}");
            }
        }
    }
}
