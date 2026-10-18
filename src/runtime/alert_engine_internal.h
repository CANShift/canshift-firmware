#pragma once

#include "alert_engine.h"
#include "config/config_types.h"

namespace AlertEngineInternal {

constexpr int kWholeDecimals = 0;

struct FixedText {
    char text[16];
};

FixedText fixed(float value, int decimals);

AlertEngine::AlertLevel maxLevel(AlertEngine::AlertLevel a, AlertEngine::AlertLevel b);

AlertEngine::AlertLevel withSensorLost(AlertEngine::AlertLevel level, bool lost);

}

namespace AlertDeclaredTemps {

void reset();

void bind(const CfgSignalDef &def);

void step(const SignalStore::SignalValue *snap, uint32_t now, AlertEngine::AlertState &state);

AlertEngine::AlertLevel highest(const AlertEngine::AlertState &state,
                                AlertEngine::AlertLevel floor);

void logArming();

bool criticalLimitFor(SignalId id, float reading, AlertEngine::CriticalLimit &out);

}
