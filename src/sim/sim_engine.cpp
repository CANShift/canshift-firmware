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

static float s_rpm = SIM_RPM_MIN;
static int8_t s_rpmDir = 1;         // +1 = rising, -1 = falling
static float s_coolantTemp = 20.0f; // Cold start, ramps up
static float s_oilTemp = 20.0f;
static float s_oilPressure = SIM_OIL_PRESS_BAR;
static float s_batteryVolts = 12.8f;
static float s_lambda = 1.0f;
static uint8_t s_gear = 1;
static float s_speed = 0.0f;
static float s_iat = 25.0f;
static float s_map = 100.0f;
static float s_boost = 0.0f;
static float s_fuelPress = 4.0f;
static uint32_t s_tick = 0;
static bool s_milFlash = false;
static bool s_warmedUp = false;

// Sweep around operating temp once warmup completes — amplitude crosses
// configured warn/danger thresholds so alert paths are exercised in sim.
static constexpr float SIM_COOLANT_SWEEP_AMP_C = 25.0f;
static constexpr float SIM_COOLANT_SWEEP_FREQ = 0.005f;
static constexpr float SIM_OIL_TEMP_SWEEP_AMP_C = 50.0f;
static constexpr float SIM_OIL_TEMP_SWEEP_FREQ = 0.004f;
static constexpr float SIM_BATTERY_CENTER_V = 13.5f;
static constexpr float SIM_BATTERY_AMP_V = 2.6f;
static constexpr float SIM_BATTERY_FREQ = 0.01f;
static constexpr float SIM_IAT_ENVELOPE_AMP_C = 15.0f;
static constexpr float SIM_IAT_ENVELOPE_FREQ = 0.003f;
static constexpr float SIM_FUEL_PRESS_CENTER_BAR = 4.0f;
static constexpr float SIM_FUEL_PRESS_AMP_BAR = 1.5f;
static constexpr float SIM_FUEL_PRESS_FREQ = 0.02f;
static constexpr float SIM_SPEED_MAX_KPH = 295.0f;
static constexpr float SIM_TURBO_KICKIN_RPM = 6000.0f;
static constexpr float SIM_TURBO_PEAK_KPA = 280.0f;

// ---------------------------------------------------------------------------
// Simulation helpers
// ---------------------------------------------------------------------------

static float simLambda(float rpm) {
    // Warmup enrichment for the first ~5 s, then sweep across stoich.
    if (s_tick < (5000 / SIM_UPDATE_MS))
        return 0.85f;
    if (rpm > 5000.0f)
        return 0.85f;
    if (rpm > 2500.0f)
        return 1.0f + 0.25f * sinf(s_tick * 0.05f);
    // Periodic spike to exercise the danger threshold.
    if ((s_tick % 10) == 0)
        return 1.45f;
    return 1.0f + 0.05f * sinf(s_tick * 0.1f);
}

static uint8_t simGear(float rpm, float speed) {
    if (speed < 5.0f)
        return 0; // Neutral
    if (rpm > 5500.0f)
        return 1;
    if (rpm > 4000.0f)
        return 2;
    if (rpm > 3000.0f)
        return 3;
    if (rpm > 2000.0f)
        return 4;
    return 5;
}

static float simMapKpa(float rpm) {
    if (rpm < 2000.0f)
        return 70.0f; // Vacuum
    if (rpm < 3000.0f)
        return 100.0f; // Atmospheric
    if (rpm < SIM_TURBO_KICKIN_RPM)
        return 100.0f + (rpm - 3000.0f) * 0.04f; // Ramp 100 → 220
    // Above kick-in RPM, ramp aggressively toward the turbo peak.
    const float frac = (rpm - SIM_TURBO_KICKIN_RPM) / (SIM_RPM_MAX - SIM_TURBO_KICKIN_RPM);
    return 220.0f + (SIM_TURBO_PEAK_KPA - 220.0f) * frac;
}

static float simSpeedKph(float rpm, uint8_t gear) {
    const float effectiveGear = (gear == 0) ? 1.0f : static_cast<float>(gear);
    const float speed = (rpm / 100.0f) * (1.0f + 0.7f * (effectiveGear - 1.0f));
    return (speed > SIM_SPEED_MAX_KPH) ? SIM_SPEED_MAX_KPH : speed;
}

static void updateRpm() {
    s_rpm += SIM_RPM_STEP * s_rpmDir;
    if (s_rpm >= SIM_RPM_MAX) {
        s_rpm = SIM_RPM_MAX;
        s_rpmDir = -1;
    } else if (s_rpm <= SIM_RPM_MIN) {
        s_rpm = SIM_RPM_MIN;
        s_rpmDir = 1;
    }
}

static void updateTemperatures() {
    if (!s_warmedUp) {
        if (s_coolantTemp < SIM_COOLANT_C)
            s_coolantTemp += 0.05f;
        if (s_oilTemp < SIM_OIL_TEMP_C)
            s_oilTemp += 0.03f;
        if (s_coolantTemp >= SIM_COOLANT_C && s_oilTemp >= SIM_OIL_TEMP_C)
            s_warmedUp = true;
        return;
    }
    s_coolantTemp = SIM_COOLANT_C + SIM_COOLANT_SWEEP_AMP_C * sinf(s_tick * SIM_COOLANT_SWEEP_FREQ);
    s_oilTemp = SIM_OIL_TEMP_C + SIM_OIL_TEMP_SWEEP_AMP_C * sinf(s_tick * SIM_OIL_TEMP_SWEEP_FREQ);
}

static void updateIat() {
    const float rpmFrac = (s_rpm - SIM_RPM_MIN) / (SIM_RPM_MAX - SIM_RPM_MIN);
    const float base = 25.0f + 30.0f * rpmFrac;
    s_iat = base + SIM_IAT_ENVELOPE_AMP_C * sinf(s_tick * SIM_IAT_ENVELOPE_FREQ);
}

static void applyEdgeProbe() {
    // Inject one tick of out-of-range values every 10 s to exercise widget
    // below-min and above-max clamping paths.
    if ((s_tick % 200) != 0)
        return;
    s_iat = -25.0f;
    s_rpm = 8500.0f;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SimEngine::init() {
    s_rpm = SIM_RPM_MIN;
    s_rpmDir = 1;
    s_coolantTemp = 20.0f;
    s_oilTemp = 20.0f;
    s_iat = 25.0f;
    s_map = 100.0f;
    s_boost = 0.0f;
    s_fuelPress = SIM_FUEL_PRESS_CENTER_BAR;
    s_tick = 0;
    s_warmedUp = false;
    LOG_INFO("SIM", "Simulation engine initialized");
}

void SimEngine::tick() {
    s_tick++;

    updateRpm();
    updateTemperatures();
    updateIat();

    // Oil pressure drops at low RPM (normal behaviour).
    s_oilPressure = SIM_OIL_PRESS_BAR - (SIM_RPM_MIN / s_rpm) * 1.0f;
    if (s_oilPressure < 0.5f)
        s_oilPressure = 0.5f;

    s_lambda = simLambda(s_rpm);
    s_gear = simGear(s_rpm, s_speed);
    s_speed = simSpeedKph(s_rpm, s_gear);

    s_batteryVolts = SIM_BATTERY_CENTER_V + SIM_BATTERY_AMP_V * sinf(s_tick * SIM_BATTERY_FREQ);

    s_map = simMapKpa(s_rpm);
    s_boost = (s_map - 100.0f) / 100.0f;

    s_fuelPress =
        SIM_FUEL_PRESS_CENTER_BAR + SIM_FUEL_PRESS_AMP_BAR * sinf(s_tick * SIM_FUEL_PRESS_FREQ);

    s_milFlash = (s_tick % (10000 / SIM_UPDATE_MS) < (500 / SIM_UPDATE_MS));

    applyEdgeProbe();

    // Write all values to SignalStore
    SignalStore::update(SignalIds::RPM, s_rpm);
    SignalStore::update(SignalIds::SPEED_KPH, s_speed);
    SignalStore::update(SignalIds::COOLANT_TEMP_C, s_coolantTemp);
    SignalStore::update(SignalIds::OIL_TEMP_C, s_oilTemp);
    SignalStore::update(SignalIds::OIL_PRESS_BAR, s_oilPressure);
    SignalStore::update(SignalIds::FUEL_PRESS_BAR, s_fuelPress);
    SignalStore::update(SignalIds::LAMBDA_1, s_lambda);
    SignalStore::update(SignalIds::AFR_1, s_lambda * 14.7f);
    SignalStore::update(SignalIds::GEAR, static_cast<float>(s_gear));
    SignalStore::update(SignalIds::BATTERY_VOLTS, s_batteryVolts);
    SignalStore::update(SignalIds::THROTTLE_POS, (s_rpm / SIM_RPM_MAX) * 100.0f);
    SignalStore::update(SignalIds::MAP_KPA, s_map);
    SignalStore::update(SignalIds::BOOST_BAR, s_boost);
    SignalStore::update(SignalIds::IAT_C, s_iat);
    SignalStore::update(SignalIds::FLAG_MIL, s_milFlash ? 1.0f : 0.0f);
    SignalStore::update(SignalIds::MAP_NUMBER, 1.0f);
}

#else

// Stubs when simulation mode is disabled
void SimEngine::init() {}
void SimEngine::tick() {}

#endif // APP_SIMULATION_MODE
