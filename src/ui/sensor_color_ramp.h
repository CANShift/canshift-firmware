#pragma once

#include "config/config_types.h"

#include <stdint.h>

using RampInterp = CfgRampInterp;
using CfgRampStop = CfgRampStopDef;
using CfgColorRamp = CfgColorRampDef;

enum class SensorKind : uint8_t {
    Coolant = 0,
    OilTemp,
    OilPress,
    BatteryVolts,
    Rpm,
    Afr,
    Boost,
    IntakeTemp,
    Egt,
    Unknown,
};

constexpr uint8_t kSensorKindCount = 9;

extern const CfgColorRamp kSensorDefaultRamps[kSensorKindCount];

[[nodiscard]] SensorKind sensorKindFromName(const char *signalName);

[[nodiscard]] const CfgColorRamp *resolveRamp(const CfgColorRamp &perSignal,
                                              const char *signalName);

[[nodiscard]] uint32_t colorAtValue(const CfgColorRamp &ramp, float value);
