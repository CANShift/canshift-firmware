// sim_engine.cpp — Simulated signal generator

#include "sim_engine.h"
#include "app_config.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <math.h>

#if APP_SIMULATION_MODE

// ---------------------------------------------------------------------------
// Internal simulation state
// ---------------------------------------------------------------------------

static float  s_rpm           = SIM_RPM_MIN;
static int8_t s_rpmDir        = 1;     // +1 = rising, -1 = falling
static float  s_coolantTemp   = 20.0f; // Cold start, ramps up
static float  s_oilTemp       = 20.0f;
static float  s_oilPressure   = SIM_OIL_PRESS_BAR;
static float  s_batteryVolts  = 12.8f;
static float  s_lambda        = 1.0f;
static uint8_t s_gear         = 1;
static float  s_speed         = 0.0f;
static uint32_t s_tick        = 0;
static bool   s_milFlash      = false;

// ---------------------------------------------------------------------------
// Simulation helpers
// ---------------------------------------------------------------------------

static float simLambda(float rpm) {
    // Simulate slightly rich at high RPM, lean at idle
    if (rpm < 1000.0f) return 1.05f;
    if (rpm > 5000.0f) return 0.92f;
    return 1.0f + 0.02f * sinf(s_tick * 0.1f);
}

static uint8_t simGear(float rpm, float speed) {
    if (speed < 5.0f)  return 0;  // Neutral
    if (rpm > 5500.0f) return 1;
    if (rpm > 4000.0f) return 2;
    if (rpm > 3000.0f) return 3;
    if (rpm > 2000.0f) return 4;
    return 5;
}

static float simBoostBar(float rpm) {
    if (rpm < 2000.0f) return -0.3f;  // Vacuum
    if (rpm < 3000.0f) return 0.0f;   // Atmospheric
    return (rpm - 3000.0f) / 10000.0f;  // Slight positive boost at high RPM
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SimEngine::init() {
    s_rpm         = SIM_RPM_MIN;
    s_rpmDir      = 1;
    s_coolantTemp = 20.0f;
    s_oilTemp     = 20.0f;
    s_tick        = 0;
    LOG_INFO("SIM", "Simulation engine initialized");
}

void SimEngine::tick() {
    s_tick++;

    // RPM sweep — triangle wave from SIM_RPM_MIN to SIM_RPM_MAX
    s_rpm += SIM_RPM_STEP * s_rpmDir;
    if (s_rpm >= SIM_RPM_MAX) {
        s_rpm    = SIM_RPM_MAX;
        s_rpmDir = -1;
    } else if (s_rpm <= SIM_RPM_MIN) {
        s_rpm    = SIM_RPM_MIN;
        s_rpmDir = 1;
    }

    // Speed derived from RPM (rough approximation)
    s_speed = s_rpm / 100.0f;
    if (s_speed > 250.0f) s_speed = 250.0f;

    // Temperature ramp — slow warmup to operating temp
    if (s_coolantTemp < SIM_COOLANT_C) {
        s_coolantTemp += 0.05f;
    }
    if (s_oilTemp < SIM_OIL_TEMP_C) {
        s_oilTemp += 0.03f;
    }

    // Simulate oil pressure drop at low RPM (normal behavior)
    s_oilPressure = SIM_OIL_PRESS_BAR - (SIM_RPM_MIN / s_rpm) * 1.0f;
    if (s_oilPressure < 0.5f) s_oilPressure = 0.5f;

    // Lambda
    s_lambda = simLambda(s_rpm);

    // Gear
    s_gear = simGear(s_rpm, s_speed);

    // Battery voltage — slight variation
    s_batteryVolts = 13.8f + 0.3f * sinf(s_tick * 0.05f);

    // MIL — flash briefly every 10 seconds for testing
    s_milFlash = (s_tick % (10000 / SIM_UPDATE_MS) < (500 / SIM_UPDATE_MS));

    // Write all values to SignalStore
    SignalStore::update(SignalIds::RPM,           s_rpm);
    SignalStore::update(SignalIds::SPEED_KPH,     s_speed);
    SignalStore::update(SignalIds::COOLANT_TEMP_C, s_coolantTemp);
    SignalStore::update(SignalIds::OIL_TEMP_C,    s_oilTemp);
    SignalStore::update(SignalIds::OIL_PRESS_BAR, s_oilPressure);
    SignalStore::update(SignalIds::LAMBDA_1,      s_lambda);
    SignalStore::update(SignalIds::AFR_1,         s_lambda * 14.7f);
    SignalStore::update(SignalIds::GEAR,          static_cast<float>(s_gear));
    SignalStore::update(SignalIds::BATTERY_VOLTS, s_batteryVolts);
    SignalStore::update(SignalIds::THROTTLE_POS,  (s_rpm / SIM_RPM_MAX) * 100.0f);
    SignalStore::update(SignalIds::MAP_KPA,       100.0f + simBoostBar(s_rpm) * 100.0f);
    SignalStore::update(SignalIds::BOOST_BAR,     simBoostBar(s_rpm));
    SignalStore::update(SignalIds::IAT_C,         25.0f + 0.5f * sinf(s_tick * 0.02f));
    SignalStore::update(SignalIds::FLAG_MIL,      s_milFlash ? 1.0f : 0.0f);
    SignalStore::update(SignalIds::MAP_NUMBER,    1.0f);
}

#else

// Stubs when simulation mode is disabled
void SimEngine::init() {}
void SimEngine::tick() {}

#endif // APP_SIMULATION_MODE
