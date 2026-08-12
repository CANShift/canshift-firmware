#include "signal_stats.h"

#include <string.h>

namespace {

float s_max[SIGNAL_STORE_MAX_SIGNALS];
bool s_seen[SIGNAL_STORE_MAX_SIGNALS];

} // namespace

void SignalStats::tick(const SignalStore::SignalValue *snap) {
    if (snap == nullptr)
        return;
    for (SignalId id = 0; id < SIGNAL_STORE_MAX_SIGNALS; ++id) {
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
    return id < SIGNAL_STORE_MAX_SIGNALS && s_seen[id];
}

float SignalStats::maxValue(SignalId id) {
    return id < SIGNAL_STORE_MAX_SIGNALS ? s_max[id] : 0.0f;
}

void SignalStats::reset() {
    memset(s_max, 0, sizeof(s_max));
    memset(s_seen, 0, sizeof(s_seen));
}
