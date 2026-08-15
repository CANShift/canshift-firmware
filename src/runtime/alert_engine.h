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

void init();

void tick(const SignalStore::SignalValue *snap);

AlertState getState();

bool isRevLimiterRowLit();

} // namespace AlertEngine
