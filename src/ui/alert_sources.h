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
    AlertEngine::CriticalLimit (*limitFor)(SignalId id, float reading);
};

constexpr CriticalSource kSources[] = {
    {"OIL PRESS", "OIL PRESSURE", SignalIds::kOilPressBar, "bar", 1,
     &AlertEngine::AlertState::oilPressure, &AlertEngine::AlertState::oilPressureSensorLost,
     &AlertEngine::criticalLimitFor},
    {"COOLANT", "WATER", SignalIds::kCoolantTempC, "°C", 0, &AlertEngine::AlertState::coolantTemp,
     &AlertEngine::AlertState::coolantSensorLost, &AlertEngine::criticalLimitFor},
    {"OIL TEMP", "OIL TEMP", SignalIds::kOilTempC, "°C", 0, &AlertEngine::AlertState::oilTemp,
     &AlertEngine::AlertState::oilTempSensorLost, &AlertEngine::criticalLimitFor},
    {"BATT", "BATTERY", SignalIds::kBatteryVolts, "V", 1, &AlertEngine::AlertState::batteryVoltage,
     &AlertEngine::AlertState::batterySensorLost, &AlertEngine::criticalLimitFor},
    {"EGT", "EXHAUST TEMP", SignalIds::kEgtC, "°C", 0, &AlertEngine::AlertState::exhaustTemp,
     &AlertEngine::AlertState::exhaustSensorLost, &AlertEngine::criticalLimitFor},
    {"GEARBOX", "GEARBOX TEMP", SignalIds::kGearboxTempC, "°C", 0,
     &AlertEngine::AlertState::gearboxTemp, &AlertEngine::AlertState::gearboxSensorLost,
     &AlertEngine::criticalLimitFor},
    {"DIFF", "DIFF TEMP", SignalIds::kDiffTempC, "°C", 0, &AlertEngine::AlertState::diffTemp,
     &AlertEngine::AlertState::diffSensorLost, &AlertEngine::criticalLimitFor},
};

constexpr size_t kSourceCount = sizeof(kSources) / sizeof(kSources[0]);

[[nodiscard]] size_t count();

[[nodiscard]] const CriticalSource &at(size_t index);

} // namespace AlertSources
