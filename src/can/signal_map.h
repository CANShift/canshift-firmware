#pragma once
// signal_map.h — Signal ID registry
//
// All signal IDs used in the firmware are defined here as named constants.
// This prevents magic strings throughout the code.
// Signal IDs match the "name" field in signals.json.

#include <stdint.h>

// ---------------------------------------------------------------------------
// Signal ID type — used as key in SignalStore
// ---------------------------------------------------------------------------
using SignalId = uint8_t;

// Resolve a signal name string (matching the "id" field in signals.json) to
// its SignalId constant. Returns SignalIds::SIGNAL_COUNT for unknown names or
// nullptr — callers must check before indexing into SignalStore. Backed by
// a single table — adding a signal is a one-line change there.
SignalId signalIdFromName(const char *name);

// ---------------------------------------------------------------------------
// Well-known signal IDs — must match signals.json "id" fields
// ---------------------------------------------------------------------------
namespace SignalIds {

// Engine
static constexpr SignalId RPM = 0;
static constexpr SignalId THROTTLE_POS = 1; // TPS %
static constexpr SignalId MAP_KPA = 2;      // Manifold absolute pressure
static constexpr SignalId BOOST_BAR = 3;    // Derived: MAP - 100kPa
static constexpr SignalId IAT_C = 4;        // Intake air temperature °C

// Temperatures
static constexpr SignalId COOLANT_TEMP_C = 5;
static constexpr SignalId OIL_TEMP_C = 6;

// Pressures
static constexpr SignalId OIL_PRESS_BAR = 7;
static constexpr SignalId FUEL_PRESS_BAR = 8;

// Fueling
static constexpr SignalId LAMBDA_1 = 9; // Lambda (1.0 = stoich)
static constexpr SignalId AFR_1 = 10;   // AFR (14.7 = stoich)

// Vehicle
static constexpr SignalId SPEED_KPH = 11;
static constexpr SignalId GEAR = 12; // Estimated gear (0=neutral)

// Electrical
static constexpr SignalId BATTERY_VOLTS = 13;

// ECU status flags
static constexpr SignalId FLAG_MIL = 20; // Check engine light
static constexpr SignalId FLAG_LAUNCH_CTRL = 21;
static constexpr SignalId FLAG_FLAT_SHIFT = 22;
static constexpr SignalId FLAG_ANTI_LAG = 23;
static constexpr SignalId FLAG_TRACTION_CUT = 24;

// ECU map/profile info
static constexpr SignalId MAP_NUMBER = 30;   // Active map number
static constexpr SignalId MAP_NAME_IDX = 31; // Map name index (if ECU exposes it)

// Lap timer (if wired)
static constexpr SignalId LAP_TIMER_MS = 40;

// Sentinel
static constexpr SignalId SIGNAL_COUNT = 64;

} // namespace SignalIds
