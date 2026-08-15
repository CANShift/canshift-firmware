#pragma once

#include "alert_engine_rs.h"
#include "can/signal_map.h"
#include "runtime/signal_store.h"

#include <stddef.h>
#include <stdint.h>

namespace CutSources {

struct Clause {
    const char *label;
    SignalId signal;
    uint8_t decimals;
    const char *trailer;
};

struct Source {
    SignalId flag;
    Clause primary;
    Clause secondary;
};

constexpr size_t kSourceCount = ALERT_CUT_KIND_COUNT;

extern const Source kSources[kSourceCount];

[[nodiscard]] uint16_t activeFlags(const SignalStore::SignalValue *snap);

[[nodiscard]] SignalId offendingSignal(uint8_t kind);

void composeDetail(uint8_t kind, const SignalStore::SignalValue *snap, char *out, size_t len);

} // namespace CutSources
