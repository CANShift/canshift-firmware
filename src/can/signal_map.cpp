// signal_map.cpp — Signal name → SignalId lookup table
//
// Single source of truth for mapping the string keys used in dashboard.json
// (and CfgWidget.signalId / CfgTopBarItem.signalId) to the numeric SignalIds
// used in SignalStore. Add new signals here and in signal_map.h together.

#include "signal_map.h"

#include <string.h>

namespace {

struct NameToId {
    const char *name;
    SignalId id;
};

// Extend this table when adding a signal — it is the only place the mapping
// lives. Ordering does not matter (linear scan).
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
    if (name == nullptr || name[0] == '\0')
        return SignalIds::SIGNAL_COUNT;
    for (const auto &entry : kNameToId) {
        if (strcmp(name, entry.name) == 0)
            return entry.id;
    }
    return SignalIds::SIGNAL_COUNT;
}
