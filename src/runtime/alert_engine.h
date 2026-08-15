#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "runtime/signal_store.h"

namespace AlertEngine {

enum class AlertLevel : uint8_t { NORMAL = 0, CAUTION = 1, WARNING = 2, CRITICAL = 3 };

struct AlertState {
    AlertLevel revLimiter;
    bool revLimiterRowLit;
    AlertLevel coolantTemp;
    AlertLevel oilTemp;
    AlertLevel oilPressure;
    bool milActive;
    AlertLevel batteryVoltage;
    AlertLevel global;
    bool coolantSensorLost;
    bool oilTempSensorLost;
    bool oilPressureSensorLost;
    bool batterySensorLost;
};

struct CriticalLimit {
    float limit;
    bool below;
    bool valid;
};

void init();

void tick(const SignalStore::SignalValue *snap);

AlertState getState();

CriticalLimit criticalLimitFor(SignalId id, float reading);

bool isRevLimiterRowLit();

} // namespace AlertEngine
