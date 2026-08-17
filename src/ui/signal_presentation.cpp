#include "ui/signal_presentation.h"

#include <string.h>

namespace SignalPresentation {

namespace {

constexpr Entry kEntries[] = {
    {"rpm", "RPM", "rpm"},
    {"speed_kph", "SPEED", "km/h"},
    {"coolant_temp_c", "WATER", "\u00b0C"},
    {"oil_temp_c", "OIL T", "\u00b0C"},
    {"oil_press_bar", "OIL PRESS", "bar"},
    {"fuel_press_bar", "FUEL PRESS", "bar"},
    {"fuel_level_pct", "FUEL", "%"},
    {"map_kpa", "MAP", "kPa"},
    {"boost_bar", "BOOST", "bar"},
    {"throttle_pos", "TPS", "%"},
    {"gear", "GEAR", ""},
    {"afr_1", "AFR", ""},
    {"lambda_1", "LAMBDA", "\u03bb"},
    {"iat_c", "IAT", "\u00b0C"},
    {"egt_c", "EGT", "\u00b0C"},
    {"gearbox_temp_c", "GEARBOX", "\u00b0C"},
    {"diff_temp_c", "DIFF", "\u00b0C"},
    {"knock_count", "KNOCK", ""},
    {"clutch_state", "CLUTCH", ""},
    {"odo_km", "ODO", "km"},
    {"trip_km", "TRIP", "km"},
    {"battery_volts", "BATT", "V"},
    {"flag_mil", "MIL", ""},
    {"flag_anti_lag", "ALS", ""},
    {"flag_launch_ctrl", "LAUNCH", ""},
    {"flag_traction_cut", "TC", ""},
    {"flag_flat_shift", "FLAT SHIFT", ""},
};

const Entry *find(const char *signalId) {
    if (!signalId || signalId[0] == '\0')
        return nullptr;
    for (const Entry &entry : kEntries) {
        if (strcmp(signalId, entry.signalId) == 0)
            return &entry;
    }
    return nullptr;
}

} // namespace

const Entry *entries() {
    return kEntries;
}

size_t entryCount() {
    return sizeof(kEntries) / sizeof(kEntries[0]);
}

const char *kickerForSignal(const char *signalId) {
    const Entry *entry = find(signalId);
    return entry != nullptr ? entry->kicker : nullptr;
}

const char *unitForSignal(const char *signalId) {
    const Entry *entry = find(signalId);
    return entry != nullptr ? entry->unit : "";
}

} // namespace SignalPresentation
