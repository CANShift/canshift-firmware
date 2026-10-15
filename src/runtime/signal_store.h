#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "can/signal_map.h"
#include "app_config.h"

static_assert(kSignalStoreMaxSignals >= SignalIds::kSignalCount,
              "signal store must hold every id the catalogue can hand out");

namespace SignalStore {

struct SignalValue {
    float raw;
    float smoothed;
    uint32_t lastUpdateMs;
    bool valid;
    uint32_t timeoutMs;
};

void init();

void update(SignalId id, float value);

void set(SignalId id, float value);

[[nodiscard]] float read(SignalId id, float defaultValue = 0.0f);

[[nodiscard]] bool isValid(SignalId id);

[[nodiscard]] bool tryRead(SignalId id, float *smoothed);

[[nodiscard]] bool anyValid(const SignalId *ids, size_t count);

void snapshotAll(SignalValue out[kSignalStoreMaxSignals]);

void setTimeout(SignalId id, uint32_t timeoutMs);

void checkTimeouts();

} // namespace SignalStore
