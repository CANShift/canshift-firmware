#pragma once

#include <stdint.h>

using SignalId = uint8_t;

[[nodiscard]] SignalId signalIdFromName(const char *name);

namespace SignalIds {

static constexpr SignalId RPM = 0;
static constexpr SignalId THROTTLE_POS = 1;
static constexpr SignalId MAP_KPA = 2;
static constexpr SignalId BOOST_BAR = 3;
static constexpr SignalId IAT_C = 4;

static constexpr SignalId COOLANT_TEMP_C = 5;
static constexpr SignalId OIL_TEMP_C = 6;

static constexpr SignalId OIL_PRESS_BAR = 7;
static constexpr SignalId FUEL_PRESS_BAR = 8;

static constexpr SignalId LAMBDA_1 = 9;
static constexpr SignalId AFR_1 = 10;

static constexpr SignalId SPEED_KPH = 11;
static constexpr SignalId GEAR = 12;

static constexpr SignalId BATTERY_VOLTS = 13;

static constexpr SignalId FLAG_MIL = 20;
static constexpr SignalId FLAG_LAUNCH_CTRL = 21;
static constexpr SignalId FLAG_FLAT_SHIFT = 22;
static constexpr SignalId FLAG_ANTI_LAG = 23;
static constexpr SignalId FLAG_TRACTION_CUT = 24;

static constexpr SignalId MAP_NUMBER = 30;
static constexpr SignalId MAP_NAME_IDX = 31;

static constexpr SignalId LAP_TIMER_MS = 40;

static constexpr SignalId SIGNAL_COUNT = 64;

} // namespace SignalIds
