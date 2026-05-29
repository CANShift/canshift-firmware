// signal_map.cpp — Signal name → SignalId lookup table
//
// Single source of truth for mapping the string keys used in dashboard.json
// (and CfgWidget.signalId / CfgTopBarItem.signalId) to the numeric SignalIds
// used in SignalStore. Add new signals here and in signal_map.h together.
//
// When `USE_RUST_SIGNAL_MAP=1` is set in the PIO build, the body of
// `signalIdFromName` delegates to the Rust port (issue #1177 R-4). Keep
// `rust/signal-map/src/lib.rs` `NAME_TO_ID` in lockstep with `kNameToId`
// below — `cargo test -p signal-map` locks the IDs down, but the names
// have to be updated on both sides for new signals.

#include "signal_map.h"

#include <string.h>

#if USE_RUST_SIGNAL_MAP
    #include "signal_map_rs.h"
#endif

namespace {

struct NameToId {
    const char *name;
    SignalId id;
};

// Extend this table when adding a signal — it is the only place the mapping
// lives. Ordering does not matter (linear scan). Compiled even when the
// Rust path is active so a future "compare both backends in CI" smoke can
// pull it back in without resurrecting the table.
constexpr NameToId kNameToId[] = {
    {"rpm", SignalIds::RPM},
    {"throttle_pos", SignalIds::THROTTLE_POS},
    {"map_kpa", SignalIds::MAP_KPA},
    {"boost_bar", SignalIds::BOOST_BAR},
    {"iat_c", SignalIds::IAT_C},
    {"coolant_temp_c", SignalIds::COOLANT_TEMP_C},
    {"oil_temp_c", SignalIds::OIL_TEMP_C},
    {"oil_press_bar", SignalIds::OIL_PRESS_BAR},
    {"fuel_press_bar", SignalIds::FUEL_PRESS_BAR},
    {"lambda_1", SignalIds::LAMBDA_1},
    {"afr_1", SignalIds::AFR_1},
    {"speed_kph", SignalIds::SPEED_KPH},
    {"gear", SignalIds::GEAR},
    {"battery_volts", SignalIds::BATTERY_VOLTS},
    {"flag_mil", SignalIds::FLAG_MIL},
    {"flag_launch_ctrl", SignalIds::FLAG_LAUNCH_CTRL},
    {"flag_flat_shift", SignalIds::FLAG_FLAT_SHIFT},
    {"flag_anti_lag", SignalIds::FLAG_ANTI_LAG},
    {"flag_traction_cut", SignalIds::FLAG_TRACTION_CUT},
    {"map_number", SignalIds::MAP_NUMBER},
    {"map_name_idx", SignalIds::MAP_NAME_IDX},
    {"lap_timer_ms", SignalIds::LAP_TIMER_MS},
};

} // namespace

SignalId signalIdFromName(const char *name) {
#if USE_RUST_SIGNAL_MAP
    // Delegate to the Rust port. `signal_id_from_name_rs` already handles
    // null / empty / missing-NUL defensively, so the wrapper here is a
    // one-liner — matches the function signature exactly.
    return signal_id_from_name_rs(name);
#else
    if (name == nullptr || name[0] == '\0')
        return SignalIds::SIGNAL_COUNT;
    for (const auto &entry : kNameToId) {
        if (strcmp(name, entry.name) == 0)
            return entry.id;
    }
    return SignalIds::SIGNAL_COUNT;
#endif
}
