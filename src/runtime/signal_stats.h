#pragma once

#include "can/signal_map.h"
#include "runtime/signal_store.h"

namespace SignalStats {

void tick(const SignalStore::SignalValue *snap);

[[nodiscard]] bool hasMax(SignalId id);

[[nodiscard]] float maxValue(SignalId id);

void reset();

} // namespace SignalStats
