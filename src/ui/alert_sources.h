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
    {"OIL PRESS", "OIL PRESSURE", SignalIds::kOilPressBar, "bar", 1,
     &AlertEngine::AlertState::oilPressure, &AlertEngine::AlertState::oilPressureSensorLost},
    {"COOLANT", "WATER", SignalIds::kCoolantTempC, "°C", 0, &AlertEngine::AlertState::coolantTemp,
     &AlertEngine::AlertState::coolantSensorLost},
    {"OIL TEMP", "OIL TEMP", SignalIds::kOilTempC, "°C", 0, &AlertEngine::AlertState::oilTemp,
     &AlertEngine::AlertState::oilTempSensorLost},
    {"BATT", "BATTERY", SignalIds::kBatteryVolts, "V", 1, &AlertEngine::AlertState::batteryVoltage,
     &AlertEngine::AlertState::batterySensorLost},
    {"EGT", "EXHAUST TEMP", SignalIds::kEgtC, "°C", 0, &AlertEngine::AlertState::exhaustTemp,
     &AlertEngine::AlertState::exhaustSensorLost},
    {"GEARBOX", "GEARBOX TEMP", SignalIds::kGearboxTempC, "°C", 0,
     &AlertEngine::AlertState::gearboxTemp, &AlertEngine::AlertState::gearboxSensorLost},
    {"DIFF", "DIFF TEMP", SignalIds::kDiffTempC, "°C", 0, &AlertEngine::AlertState::diffTemp,
     &AlertEngine::AlertState::diffSensorLost},
};

constexpr size_t kSourceCount = sizeof(kSources) / sizeof(kSources[0]);

} // namespace AlertSources
