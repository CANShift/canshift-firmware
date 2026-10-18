#pragma once

#include "app_config.h"
#include "can/signal_map.h"
#include "runtime/alert_engine.h"

#include <stddef.h>

namespace AlertSources {

struct CriticalSource {
    const char *chipLabel;
    const char *takeoverName;
    SignalId id;
    const char *signalKey;
    const char *unit;
    int decimals;
};

struct BuiltInSource {
    CriticalSource info;
    AlertEngine::AlertLevel AlertEngine::AlertState::*level;
    bool AlertEngine::AlertState::*sensorLost;
};

constexpr BuiltInSource kSources[] = {
    {{"OIL PRESS", "OIL PRESSURE", SignalIds::kOilPressBar, "oil_press_bar", "bar", 1},
     &AlertEngine::AlertState::oilPressure,
     &AlertEngine::AlertState::oilPressureSensorLost},
    {{"COOLANT", "WATER", SignalIds::kCoolantTempC, "coolant_temp_c", "°C", 0},
     &AlertEngine::AlertState::coolantTemp,
     &AlertEngine::AlertState::coolantSensorLost},
    {{"OIL TEMP", "OIL TEMP", SignalIds::kOilTempC, "oil_temp_c", "°C", 0},
     &AlertEngine::AlertState::oilTemp,
     &AlertEngine::AlertState::oilTempSensorLost},
    {{"BATT", "BATTERY", SignalIds::kBatteryVolts, "battery_volts", "V", 1},
     &AlertEngine::AlertState::batteryVoltage,
     &AlertEngine::AlertState::batterySensorLost},
    {{"EGT", "EXHAUST TEMP", SignalIds::kEgtC, "egt_c", "°C", 0},
     &AlertEngine::AlertState::exhaustTemp,
     &AlertEngine::AlertState::exhaustSensorLost},
    {{"GEARBOX", "GEARBOX TEMP", SignalIds::kGearboxTempC, "gearbox_temp_c", "°C", 0},
     &AlertEngine::AlertState::gearboxTemp,
     &AlertEngine::AlertState::gearboxSensorLost},
    {{"DIFF", "DIFF TEMP", SignalIds::kDiffTempC, "diff_temp_c", "°C", 0},
     &AlertEngine::AlertState::diffTemp,
     &AlertEngine::AlertState::diffSensorLost},
};

constexpr size_t kSourceCount = sizeof(kSources) / sizeof(kSources[0]);

constexpr size_t kMaxSourceCount = kSourceCount + kConfigMaxCriticalAlerts;

[[nodiscard]] size_t count();

[[nodiscard]] const CriticalSource &at(size_t index);

[[nodiscard]] AlertEngine::AlertLevel levelAt(const AlertEngine::AlertState &state, size_t index);

[[nodiscard]] bool sensorLostAt(const AlertEngine::AlertState &state, size_t index);

[[nodiscard]] AlertEngine::CriticalLimit limitAt(size_t index, float reading);

}
