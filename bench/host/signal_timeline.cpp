#include "signal_timeline.h"

#include "can/signal_map.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"

#include <cstring>

namespace {

struct Seen {
    bool tracked;
    bool valid;
    float raw;
    const char *name;
};

Seen s_seen[kSignalStoreMaxSignals];

void trackConfiguredSignals() {
    const CfgSignalConfig &cfg = ConfigLoader::getSignalConfig();
    for (uint8_t i = 0; i < cfg.signalCount; ++i) {
        const SignalId sid = signalIdFromName(cfg.signals[i].name);
        if (sid >= kSignalStoreMaxSignals)
            continue;
        s_seen[sid] = {true, false, 0.0f, cfg.signals[i].name};
    }
}

bool changed(const Seen &seen, const SignalStore::SignalValue &now) {
    if (seen.valid != now.valid)
        return true;
    return now.valid && memcmp(&seen.raw, &now.raw, sizeof(float)) != 0;
}

void writeChange(FILE *out, uint32_t atMs, const Seen &seen, const SignalStore::SignalValue &now) {
    if (!now.valid) {
        fprintf(out, "%8u   %s invalid\n", atMs, seen.name);
        return;
    }
    fprintf(out, "%8u   %s=%.6g\n", atMs, seen.name, static_cast<double>(now.raw));
}

} // namespace

namespace SignalTimeline {

void reset() {
    memset(s_seen, 0, sizeof(s_seen));
    trackConfiguredSignals();
}

void writeFrame(FILE *out, uint32_t atMs, const char *direction, uint32_t id, const uint8_t *data,
                uint8_t length) {
    fprintf(out, "%8u %s 0x%03X%s", atMs, direction, static_cast<unsigned>(id), length ? " " : "");
    for (uint8_t i = 0; i < length; ++i)
        fprintf(out, "%02X", data[i]);
    fprintf(out, "\n");
}

void writeChanges(FILE *out, uint32_t atMs) {
    SignalStore::SignalValue now[kSignalStoreMaxSignals];
    SignalStore::snapshotAll(now);
    for (SignalId sid = 0; sid < kSignalStoreMaxSignals; ++sid) {
        Seen &seen = s_seen[sid];
        if (!seen.tracked || !changed(seen, now[sid]))
            continue;
        writeChange(out, atMs, seen, now[sid]);
        seen.valid = now[sid].valid;
        seen.raw = now[sid].raw;
    }
}

} // namespace SignalTimeline
