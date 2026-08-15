#pragma once

#include "can/signal_map.h"
#include "runtime/signal_store.h"
#include "ui/severity.h"

namespace CutBand {

void init();

void reapplyTheme();

void update(const SignalStore::SignalValue *snap);

[[nodiscard]] Severity::Level levelFor(SignalId signal);

} // namespace CutBand
