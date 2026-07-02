#include "alert_engine.h"
#include "signal_store.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "config/config_types.h"
#include "app_config.h"
#include "diag/logger.h"
#include "alert_engine_rs.h"

#include <Arduino.h>
#include <cmath>
#include <string.h>

static AlertEngine::AlertState s_state = {};
static uint32_t s_lastFlashToggleMs = 0;
static bool s_flashPhase = false;

static float s_revLimitRpm = 7200.0f;

static float s_coolantWarnC = 100.0f;
static float s_coolantCritC = 110.0f;
static float s_coolantHighWarnC = NAN;
static float s_coolantHighCritC = NAN;
static float s_oilTempWarnC = 120.0f;
static float s_oilTempCritC = 135.0f;
static float s_oilTempHighWarnC = NAN;
static float s_oilTempHighCritC = NAN;
static float s_oilPressWarnBar = 1.5f;
static float s_oilPressCritBar = 1.0f;
static float s_oilPressHighWarnBar = NAN;
static float s_oilPressHighCritBar = NAN;
static float s_batteryLowWarnV = BATTERY_DEFAULT_LOW_WARN_V;
static float s_batteryLowCritV = BATTERY_DEFAULT_LOW_CRIT_V;
static float s_batteryHighWarnV = BATTERY_DEFAULT_HIGH_WARN_V;
static float s_batteryHighCritV = BATTERY_DEFAULT_HIGH_CRIT_V;

static constexpr uint32_t FLASH_PERIOD_MS = 1000 / (ALERT_REVLIMIT_FLASH_HZ * 2);

namespace {

struct SensorHealthSlot {
    const char *name;
    SignalId id;
    bool AlertEngine::AlertState::*lost;
    AlertSensorHealthRs health;
};

SensorHealthSlot s_sensorHealth[] = {
    {"coolant temp", SignalIds::COOLANT_TEMP_C, &AlertEngine::AlertState::coolantSensorLost, {}},
    {"oil temp", SignalIds::OIL_TEMP_C, &AlertEngine::AlertState::oilTempSensorLost, {}},
    {"oil pressure", SignalIds::OIL_PRESS_BAR, &AlertEngine::AlertState::oilPressureSensorLost, {}},
    {"battery", SignalIds::BATTERY_VOLTS, &AlertEngine::AlertState::batterySensorLost, {}},
};

AlertLevelHoldRs s_coolantHold = {};
AlertLevelHoldRs s_oilTempHold = {};
AlertLevelHoldRs s_oilPressureHold = {};
AlertLevelHoldRs s_batteryHold = {};

AlertEngine::AlertLevel maxLevel(AlertEngine::AlertLevel a, AlertEngine::AlertLevel b) {
    return (static_cast<uint8_t>(a) > static_cast<uint8_t>(b)) ? a : b;
}

void updateSensorHealth(const SignalStore::SignalValue *snap, uint32_t now) {
    for (SensorHealthSlot &slot : s_sensorHealth) {
        const bool wasLost = slot.health.lost;
        alert_sensor_health_step_rs(&slot.health, snap[slot.id].valid, now,
                                    ALERT_SENSOR_LOST_CLEAR_HOLD_MS);
        s_state.*(slot.lost) = slot.health.lost;
        if (slot.health.lost != wasLost) {
            if (slot.health.lost) {
                LOG_WARN("ALERT", "%s sensor lost", slot.name);
            } else {
                LOG_INFO("ALERT", "%s sensor recovered", slot.name);
            }
        }
    }
}

AlertEngine::AlertLevel withSensorLost(AlertEngine::AlertLevel level, bool lost) {
    return lost ? maxLevel(level, AlertEngine::AlertLevel::WARNING) : level;
}

AlertEngine::AlertLevel evalRevLimiter(float rpm) {
    return static_cast<AlertEngine::AlertLevel>(alert_eval_rev_limiter_rs(
        rpm, s_revLimitRpm, ALERT_REVLIMIT_WARN_PCT, ALERT_REVLIMIT_FLASH_PCT));
}

AlertEngine::AlertLevel stepCoolantTemp(float tempC, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_coolant_temp_step_rs(
        &s_coolantHold, tempC, s_coolantWarnC, s_coolantCritC, s_coolantHighWarnC,
        s_coolantHighCritC, now, ALERT_HYSTERESIS_PCT, ALERT_MIN_ACTIVE_MS));
}

AlertEngine::AlertLevel stepOilTemp(float tempC, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_oil_temp_step_rs(
        &s_oilTempHold, tempC, s_oilTempWarnC, s_oilTempCritC, s_oilTempHighWarnC,
        s_oilTempHighCritC, now, ALERT_HYSTERESIS_PCT, ALERT_MIN_ACTIVE_MS));
}

AlertEngine::AlertLevel stepOilPressure(float pressBar, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_oil_pressure_step_rs(
        &s_oilPressureHold, pressBar, s_oilPressWarnBar, s_oilPressCritBar, s_oilPressHighWarnBar,
        s_oilPressHighCritBar, now, ALERT_HYSTERESIS_PCT, ALERT_MIN_ACTIVE_MS));
}

AlertEngine::AlertLevel stepBattery(float volts, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_battery_step_rs(
        &s_batteryHold, volts, s_batteryLowWarnV, s_batteryLowCritV, s_batteryHighWarnV,
        s_batteryHighCritV, now, ALERT_HYSTERESIS_PCT, ALERT_MIN_ACTIVE_MS));
}

} // namespace

void AlertEngine::init() {
    s_state = {};
    s_lastFlashToggleMs = 0;
    s_flashPhase = false;
    for (SensorHealthSlot &slot : s_sensorHealth)
        slot.health = {};
    s_coolantHold = {};
    s_oilTempHold = {};
    s_oilPressureHold = {};
    s_batteryHold = {};

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (dash.loaded && dash.revLimitRpm > 0.0f)
        s_revLimitRpm = dash.revLimitRpm;

    struct ThresholdBinding {
        const char *name;
        float *warn;
        float *crit;
        float *highWarn;
        float *highCrit;
    };
    static constexpr ThresholdBinding kBindings[] = {
        {"coolant_temp_c", &s_coolantWarnC, &s_coolantCritC, &s_coolantHighWarnC,
         &s_coolantHighCritC},
        {"oil_temp_c", &s_oilTempWarnC, &s_oilTempCritC, &s_oilTempHighWarnC, &s_oilTempHighCritC},
        {"oil_press_bar", &s_oilPressWarnBar, &s_oilPressCritBar, &s_oilPressHighWarnBar,
         &s_oilPressHighCritBar},
        {"battery_volts", &s_batteryLowWarnV, &s_batteryLowCritV, &s_batteryHighWarnV,
         &s_batteryHighCritV},
    };

    const CfgSignalConfig &sigCfg = ConfigLoader::getSignalConfig();
    for (uint8_t i = 0; i < sigCfg.signalCount; i++) {
        const CfgSignalDef &def = sigCfg.signals[i];
        for (const ThresholdBinding &b : kBindings) {
            if (strcmp(def.name, b.name) != 0)
                continue;
            if (!isnan(def.warningLevel))
                *b.warn = def.warningLevel;
            if (!isnan(def.dangerLevel))
                *b.crit = def.dangerLevel;
            if (!isnan(def.highWarningLevel))
                *b.highWarn = def.highWarningLevel;
            if (!isnan(def.highDangerLevel))
                *b.highCrit = def.highDangerLevel;
            break;
        }
    }

    auto x100 = [](float v) { return static_cast<int>(lroundf(v * 100.0f)); };
    const int revLimit = static_cast<int>(lroundf(s_revLimitRpm));
    const int coolantWarn = static_cast<int>(lroundf(s_coolantWarnC));
    const int coolantCrit = static_cast<int>(lroundf(s_coolantCritC));
    const int oilTempWarn = static_cast<int>(lroundf(s_oilTempWarnC));
    const int oilTempCrit = static_cast<int>(lroundf(s_oilTempCritC));
    const int oilPressWarn100 = x100(s_oilPressWarnBar);
    const int oilPressCrit100 = x100(s_oilPressCritBar);
    const int battLow100 = x100(s_batteryLowWarnV);
    const int battLowCrit100 = x100(s_batteryLowCritV);
    const int battHigh100 = x100(s_batteryHighWarnV);
    const int battHighCrit100 = x100(s_batteryHighCritV);
    LOG_INFO("ALERT",
             "Alert engine initialized (revLimit=%d RPM, coolant warn=%d crit=%d, "
             "oilT warn=%d crit=%d, oilP warn=%d.%02d crit=%d.%02d, "
             "batt lowWarn=%d.%02d lowCrit=%d.%02d highWarn=%d.%02d highCrit=%d.%02d)",
             revLimit, coolantWarn, coolantCrit, oilTempWarn, oilTempCrit, oilPressWarn100 / 100,
             oilPressWarn100 % 100, oilPressCrit100 / 100, oilPressCrit100 % 100, battLow100 / 100,
             battLow100 % 100, battLowCrit100 / 100, battLowCrit100 % 100, battHigh100 / 100,
             battHigh100 % 100, battHighCrit100 / 100, battHighCrit100 % 100);
}

void AlertEngine::tick() {
    SignalStore::SignalValue snap[SIGNAL_STORE_MAX_SIGNALS];
    SignalStore::snapshotAll(snap);
    const uint32_t now = millis();
    updateSensorHealth(snap, now);

    const auto readSnap = [&snap](SignalId id, float def) {
        return snap[id].valid ? snap[id].smoothed : def;
    };
    float rpm = readSnap(SignalIds::RPM, 0.0f);
    float coolant = readSnap(SignalIds::COOLANT_TEMP_C, 0.0f);
    float oilTemp = readSnap(SignalIds::OIL_TEMP_C, 0.0f);
    float oilPres = readSnap(SignalIds::OIL_PRESS_BAR, 5.0f);
    float volts = readSnap(SignalIds::BATTERY_VOLTS, 13.0f);
    float mil = readSnap(SignalIds::FLAG_MIL, 0.0f);

    s_state.revLimiter = evalRevLimiter(rpm);
    s_state.coolantTemp = withSensorLost(stepCoolantTemp(coolant, now), s_state.coolantSensorLost);
    s_state.oilTemp = withSensorLost(stepOilTemp(oilTemp, now), s_state.oilTempSensorLost);
    s_state.oilPressure =
        withSensorLost(stepOilPressure(oilPres, now), s_state.oilPressureSensorLost);
    s_state.milActive = (mil > 0.5f);
    s_state.batteryVoltage = withSensorLost(stepBattery(volts, now), s_state.batterySensorLost);

    s_state.global = s_state.revLimiter;
    s_state.global = maxLevel(s_state.global, s_state.coolantTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilPressure);
    s_state.global = maxLevel(s_state.global, s_state.batteryVoltage);

    bool revCritical = (s_state.revLimiter == AlertLevel::CRITICAL);
    if (revCritical) {
        if (now - s_lastFlashToggleMs >= FLASH_PERIOD_MS) {
            s_flashPhase = !s_flashPhase;
            s_lastFlashToggleMs = now;
        }
        s_state.revLimiterFlashActive = s_flashPhase;
    } else {
        s_state.revLimiterFlashActive = false;
        s_flashPhase = false;
    }
}

AlertEngine::AlertState AlertEngine::getState() {
    return s_state;
}

bool AlertEngine::isRevLimiterFlashOn() {
    return s_state.revLimiterFlashActive;
}

uint32_t AlertEngine::getRevLimiterOverlayColor() {
    if (s_state.revLimiterFlashActive)
        return 0xFF0000;
    return 0x000000;
}
