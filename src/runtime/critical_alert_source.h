#pragma once

#include "can/signal_map.h"
#include "config/config_types.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"

#include <stddef.h>
#include <stdint.h>

namespace CriticalAlertSource {

struct Armed {
    bool armed;
    SignalId id;
    float firesAt;
    bool below;
};

void init(const CfgCriticalAlert *alerts, size_t count);

[[nodiscard]] size_t count();

[[nodiscard]] AlertEngine::AlertLevel step(size_t index, const SignalStore::SignalValue *snap,
                                           uint32_t now, bool *sensorLost);

[[nodiscard]] Armed armed(size_t index);

}
