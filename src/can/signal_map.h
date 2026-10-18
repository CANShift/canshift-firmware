#pragma once

#include <stdint.h>

using SignalId = uint8_t;

[[nodiscard]] SignalId signalIdFromName(const char *name);

namespace SignalIds {

static constexpr SignalId kRpm = 0;
static constexpr SignalId kThrottlePos = 1;
static constexpr SignalId kMapKpa = 2;
static constexpr SignalId kBoostBar = 3;
static constexpr SignalId kIatC = 4;

static constexpr SignalId kCoolantTempC = 5;
static constexpr SignalId kOilTempC = 6;

static constexpr SignalId kOilPressBar = 7;
static constexpr SignalId kFuelPressBar = 8;

static constexpr SignalId kLambda1 = 9;
static constexpr SignalId kAfr1 = 10;

static constexpr SignalId kSpeedKph = 11;
static constexpr SignalId kGear = 12;

static constexpr SignalId kBatteryVolts = 13;
static constexpr SignalId kFuelLevelPct = 14;

static constexpr SignalId kEgtC = 15;
static constexpr SignalId kGearboxTempC = 16;
static constexpr SignalId kDiffTempC = 17;
static constexpr SignalId kKnockCount = 18;
static constexpr SignalId kKnockCyl = 19;

static constexpr SignalId kFlagMil = 20;
static constexpr SignalId kFlagLaunchCtrl = 21;
static constexpr SignalId kFlagFlatShift = 22;
static constexpr SignalId kFlagAntiLag = 23;
static constexpr SignalId kFlagTractionCut = 24;
static constexpr SignalId kClutchState = 25;
static constexpr SignalId kFlagBrake = 26;
static constexpr SignalId kFlagPitLimit = 27;
static constexpr SignalId kFlagCruise = 28;
static constexpr SignalId kBoostTargetBar = 29;

static constexpr SignalId kMapNumber = 30;
static constexpr SignalId kMapNameIdx = 31;

static constexpr SignalId kOdoKm = 32;
static constexpr SignalId kTripKm = 33;

static constexpr SignalId kLapTimerMs = 40;

static constexpr SignalId kFlagBoostCut = 48;
static constexpr SignalId kFlagFuelCut = 49;
static constexpr SignalId kFlagIgnitionCut = 50;
static constexpr SignalId kFlagIgnitionRetard = 51;
static constexpr SignalId kFlagRevLimit = 52;
static constexpr SignalId kFlagPitLimitCut = 53;
static constexpr SignalId kFlagOverheatProtect = 54;
static constexpr SignalId kFlagLimpMode = 55;

static constexpr SignalId kWheelButton1 = 56;
static constexpr SignalId kWheelButton2 = 57;
static constexpr SignalId kWheelButton3 = 58;
static constexpr SignalId kWheelButton4 = 59;

static constexpr SignalId kSignalCount = 64;

}
