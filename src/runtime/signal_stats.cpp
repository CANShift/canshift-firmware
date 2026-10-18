#include "signal_stats.h"

#include <string.h>

namespace {

float s_max[kSignalStoreMaxSignals];
bool s_seen[kSignalStoreMaxSignals];

}

void SignalStats::tick(const SignalStore::SignalValue *snap) {
    if (snap == nullptr)
        return;
    for (SignalId id = 0; id < kSignalStoreMaxSignals; ++id) {
        if (!snap[id].valid)
            continue;
        const float value = snap[id].raw;
        if (!s_seen[id] || value > s_max[id]) {
            s_max[id] = value;
            s_seen[id] = true;
        }
    }
}

bool SignalStats::hasMax(SignalId id) {
    return id < kSignalStoreMaxSignals && s_seen[id];
}

float SignalStats::maxValue(SignalId id) {
    return id < kSignalStoreMaxSignals ? s_max[id] : 0.0f;
}

void SignalStats::reset() {
    memset(s_max, 0, sizeof(s_max));
    memset(s_seen, 0, sizeof(s_seen));
}
