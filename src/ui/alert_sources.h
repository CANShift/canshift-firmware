#pragma once

#include "can/signal_map.h"
#include "runtime/alert_engine.h"

namespace AlertSources {

struct CriticalSource {
    const char *chipLabel;
    const char *takeoverName;
    SignalId id;
    const char *unit;
    int decimals;
    AlertEngine::AlertLevel AlertEngine::AlertState::*level;
    bool AlertEngine::AlertState::*sensorLost;
};

constexpr CriticalSource kSources[] = {
    {"OIL PRESS", "OIL PRESSURE", SignalIds::OIL_PRESS_BAR, "bar", 1,
     &AlertEngine::AlertState::oilPressure, &AlertEngine::AlertState::oilPressureSensorLost},
    {"COOLANT", "WATER", SignalIds::COOLANT_TEMP_C, "°C", 0, &AlertEngine::AlertState::coolantTemp,
     &AlertEngine::AlertState::coolantSensorLost},
    {"OIL TEMP", "OIL TEMP", SignalIds::OIL_TEMP_C, "°C", 0, &AlertEngine::AlertState::oilTemp,
     &AlertEngine::AlertState::oilTempSensorLost},
    {"BATT", "BATTERY", SignalIds::BATTERY_VOLTS, "V", 1, &AlertEngine::AlertState::batteryVoltage,
     &AlertEngine::AlertState::batterySensorLost},
};

constexpr size_t kSourceCount = sizeof(kSources) / sizeof(kSources[0]);

} // namespace AlertSources
