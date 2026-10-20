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
    {"boost_target_bar", "TARGET", "bar"},
    {"throttle_pos", "TPS", "%"},
    {"gear", "GEAR", ""},
    {"afr_1", "AFR", ""},
    {"lambda_1", "LAMBDA", "\u03bb"},
    {"iat_c", "IAT", "\u00b0C"},
    {"maf_gps", "MAF", "g/s"},
    {"run_time_s", "RUN TIME", "s"},
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

constexpr const char *kUnitSuffixes[] = {"c",   "s",   "ms",    "gps", "kpa",
                                         "pct", "bar", "volts", "kph", "km"};

constexpr char kWordSeparator = '_';

bool isUnitSuffix(const char *word, size_t length) {
    for (const char *suffix : kUnitSuffixes) {
        if (strlen(suffix) == length && strncmp(word, suffix, length) == 0)
            return true;
    }
    return false;
}

size_t withoutTrailingSeparators(const char *signalId, size_t end) {
    while (end > 0 && signalId[end - 1] == kWordSeparator)
        --end;
    return end;
}

size_t lastWordStart(const char *signalId, size_t end) {
    while (end > 0 && signalId[end - 1] != kWordSeparator)
        --end;
    return end;
}

size_t nameLength(const char *signalId) {
    const size_t end = withoutTrailingSeparators(signalId, strlen(signalId));
    const size_t start = lastWordStart(signalId, end);
    const size_t nameEnd = withoutTrailingSeparators(signalId, start);
    return nameEnd > 0 && isUnitSuffix(signalId + start, end - start) ? nameEnd : end;
}

char upperCase(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

const Entry *find(const char *signalId) {
    if (!signalId || signalId[0] == '\0')
        return nullptr;
    for (const Entry &entry : kEntries) {
        if (strcmp(signalId, entry.signalId) == 0)
            return &entry;
    }
    return nullptr;
}

}

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

void fallbackKicker(const char *signalId, char *out, size_t outLen) {
    if (!out || outLen == 0)
        return;
    const size_t length = signalId ? nameLength(signalId) : 0;
    size_t written = 0;
    bool pendingSpace = false;
    for (size_t i = 0; i < length && written + 1 < outLen; ++i) {
        if (signalId[i] == kWordSeparator) {
            pendingSpace = written > 0;
            continue;
        }
        if (pendingSpace && written + 2 < outLen)
            out[written++] = ' ';
        pendingSpace = false;
        out[written++] = upperCase(signalId[i]);
    }
    out[written] = '\0';
}

const char *unitForSignal(const char *signalId) {
    const Entry *entry = find(signalId);
    return entry != nullptr ? entry->unit : "";
}

}
