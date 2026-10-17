#include "alert_engine.h"
#include "signal_store.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "config/config_types.h"
#include "app_config.h"
#include "diag/logger.h"
#include "alert_engine_rs.h"
#include "critical_alert_source.h"
#include "util/format_float.h"

#include <Arduino.h>
#include <cmath>
#include <string.h>

static AlertEngine::AlertState s_state = {};
static uint32_t s_revLimiterSinceMs = 0;
static bool s_revLimiterEngaged = false;

struct Thresholds {
    float warn;
    float crit;
    float highWarn;
    float highCrit;
};

constexpr float kDefaultRevLimitRpm = 7200.0f;
constexpr Thresholds kCoolantDefaults = {100.0f, 110.0f, NAN, NAN};
constexpr Thresholds kOilTempDefaults = {120.0f, 135.0f, NAN, NAN};
constexpr Thresholds kOilPressureDefaults = {1.5f, 1.0f, NAN, NAN};
constexpr Thresholds kBatteryDefaults = {kBatteryDefaultLowWarnV, kBatteryDefaultLowCritV,
                                         kBatteryDefaultHighWarnV, kBatteryDefaultHighCritV};

static float s_revLimitRpm = kDefaultRevLimitRpm;

static float s_coolantWarnC = kCoolantDefaults.warn;
static float s_coolantCritC = kCoolantDefaults.crit;
static float s_coolantHighWarnC = kCoolantDefaults.highWarn;
static float s_coolantHighCritC = kCoolantDefaults.highCrit;
static float s_oilTempWarnC = kOilTempDefaults.warn;
static float s_oilTempCritC = kOilTempDefaults.crit;
static float s_oilTempHighWarnC = kOilTempDefaults.highWarn;
static float s_oilTempHighCritC = kOilTempDefaults.highCrit;
static float s_oilPressWarnBar = kOilPressureDefaults.warn;
static float s_oilPressCritBar = kOilPressureDefaults.crit;
static float s_oilPressHighWarnBar = kOilPressureDefaults.highWarn;
static float s_oilPressHighCritBar = kOilPressureDefaults.highCrit;
static float s_batteryLowWarnV = kBatteryDefaults.warn;
static float s_batteryLowCritV = kBatteryDefaults.crit;
static float s_batteryHighWarnV = kBatteryDefaults.highWarn;
static float s_batteryHighCritV = kBatteryDefaults.highCrit;

namespace {

void stepRevLimitBlink(bool engaged, uint32_t now) {
    if (!engaged) {
        s_revLimiterEngaged = false;
        s_state.revLimiterRowLit = false;
        return;
    }
    if (!s_revLimiterEngaged) {
        s_revLimiterEngaged = true;
        s_revLimiterSinceMs = now;
    }
    s_state.revLimiterRowLit =
        alert_rev_limit_row_lit_rs(now - s_revLimiterSinceMs, kAlertRevlimitFlashHz);
}

struct SensorHealthSlot {
    const char *name;
    SignalId id;
    bool AlertEngine::AlertState::*lost;
    AlertSensorHealthRs health;
};

SensorHealthSlot s_sensorHealth[] = {
    {"coolant temp", SignalIds::kCoolantTempC, &AlertEngine::AlertState::coolantSensorLost, {}},
    {"oil temp", SignalIds::kOilTempC, &AlertEngine::AlertState::oilTempSensorLost, {}},
    {"oil pressure", SignalIds::kOilPressBar, &AlertEngine::AlertState::oilPressureSensorLost, {}},
    {"battery", SignalIds::kBatteryVolts, &AlertEngine::AlertState::batterySensorLost, {}},
};

struct TempLimits {
    float warnC;
    float critC;
};

struct DeclaredTempSource {
    const char *name;
    SignalId id;
    AlertEngine::AlertLevel AlertEngine::AlertState::*level;
    bool AlertEngine::AlertState::*lost;
    TempLimits defaults;
    TempLimits limits;
    AlertLevelHoldRs hold;
    AlertSensorHealthRs health;
    bool declared;
};

DeclaredTempSource s_declaredTemps[] = {
    {"gearbox temp",
     SignalIds::kGearboxTempC,
     &AlertEngine::AlertState::gearboxTemp,
     &AlertEngine::AlertState::gearboxSensorLost,
     {120.0f, 140.0f},
     {},
     {},
     {},
     false},
    {"diff temp",
     SignalIds::kDiffTempC,
     &AlertEngine::AlertState::diffTemp,
     &AlertEngine::AlertState::diffSensorLost,
     {120.0f, 140.0f},
     {},
     {},
     {},
     false},
    {"exhaust temp",
     SignalIds::kEgtC,
     &AlertEngine::AlertState::exhaustTemp,
     &AlertEngine::AlertState::exhaustSensorLost,
     {950.0f, 1000.0f},
     {},
     {},
     {},
     false},
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
                                    kAlertSensorLostClearHoldMs);
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
        rpm, s_revLimitRpm, kAlertRevlimitWarnPct, kAlertRevlimitFlashPct));
}

AlertEngine::AlertLevel stepCoolantTemp(float tempC, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_coolant_temp_step_rs(
        &s_coolantHold, tempC, s_coolantWarnC, s_coolantCritC, s_coolantHighWarnC,
        s_coolantHighCritC, now, kAlertHysteresisPct, kAlertMinActiveMs));
}

AlertEngine::AlertLevel stepOilTemp(float tempC, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_high_side_temp_step_rs(
        &s_oilTempHold, tempC, s_oilTempWarnC, s_oilTempCritC, s_oilTempHighWarnC,
        s_oilTempHighCritC, now, kAlertHysteresisPct, kAlertMinActiveMs));
}

AlertEngine::AlertLevel stepOilPressure(float pressBar, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_oil_pressure_step_rs(
        &s_oilPressureHold, pressBar, s_oilPressWarnBar, s_oilPressCritBar, s_oilPressHighWarnBar,
        s_oilPressHighCritBar, now, kAlertHysteresisPct, kAlertMinActiveMs));
}

struct CriticalLimitBinding {
    SignalId id;
    const float *primary;
    bool primaryBelow;
    const float *highCrit;
};

constexpr CriticalLimitBinding kCriticalLimits[] = {
    {SignalIds::kCoolantTempC, &s_coolantCritC, false, &s_coolantHighCritC},
    {SignalIds::kOilTempC, &s_oilTempCritC, false, &s_oilTempHighCritC},
    {SignalIds::kOilPressBar, &s_oilPressCritBar, true, &s_oilPressHighCritBar},
    {SignalIds::kBatteryVolts, &s_batteryLowCritV, true, &s_batteryHighCritV},
};

void bindDeclaredTemp(const CfgSignalDef &def) {
    for (DeclaredTempSource &src : s_declaredTemps) {
        if (signalIdFromName(def.name) != src.id)
            continue;
        src.declared = true;
        if (!std::isnan(def.warningLevel))
            src.limits.warnC = def.warningLevel;
        if (!std::isnan(def.dangerLevel))
            src.limits.critC = def.dangerLevel;
        return;
    }
}

void stepDeclaredTemps(const SignalStore::SignalValue *snap, uint32_t now) {
    for (DeclaredTempSource &src : s_declaredTemps) {
        if (!src.declared)
            continue;
        alert_sensor_health_step_rs(&src.health, snap[src.id].valid, now,
                                    kAlertSensorLostClearHoldMs);
        s_state.*(src.lost) = src.health.lost;
        const float value = snap[src.id].valid ? snap[src.id].raw : 0.0f;
        const auto level = static_cast<AlertEngine::AlertLevel>(
            alert_high_side_temp_step_rs(&src.hold, value, src.limits.warnC, src.limits.critC, NAN,
                                         NAN, now, kAlertHysteresisPct, kAlertMinActiveMs));
        s_state.*(src.level) = withSensorLost(level, src.health.lost);
    }
}

void stepCriticalAlerts(const SignalStore::SignalValue *snap, uint32_t now) {
    for (size_t i = 0; i < kConfigMaxCriticalAlerts; ++i)
        s_state.userCritical[i] =
            CriticalAlertSource::step(i, snap, now, &s_state.userSensorLost[i]);
}

struct ThresholdBinding {
    const char *name;
    float *warn;
    float *crit;
    float *highWarn;
    float *highCrit;
    Thresholds defaults;
};

constexpr ThresholdBinding kBindings[] = {
    {"coolant_temp_c", &s_coolantWarnC, &s_coolantCritC, &s_coolantHighWarnC, &s_coolantHighCritC,
     kCoolantDefaults},
    {"oil_temp_c", &s_oilTempWarnC, &s_oilTempCritC, &s_oilTempHighWarnC, &s_oilTempHighCritC,
     kOilTempDefaults},
    {"oil_press_bar", &s_oilPressWarnBar, &s_oilPressCritBar, &s_oilPressHighWarnBar,
     &s_oilPressHighCritBar, kOilPressureDefaults},
    {"battery_volts", &s_batteryLowWarnV, &s_batteryLowCritV, &s_batteryHighWarnV,
     &s_batteryHighCritV, kBatteryDefaults},
};

void resetSignalBindings() {
    for (const ThresholdBinding &b : kBindings) {
        *b.warn = b.defaults.warn;
        *b.crit = b.defaults.crit;
        *b.highWarn = b.defaults.highWarn;
        *b.highCrit = b.defaults.highCrit;
    }
    for (DeclaredTempSource &src : s_declaredTemps) {
        src.limits = src.defaults;
        src.declared = false;
        src.hold = {};
        src.health = {};
    }
}

void overrideIfSet(float *slot, float configured) {
    if (!std::isnan(configured))
        *slot = configured;
}

void bindThresholds(const CfgSignalDef &def) {
    for (const ThresholdBinding &b : kBindings) {
        if (strcmp(def.name, b.name) != 0)
            continue;
        overrideIfSet(b.warn, def.warningLevel);
        overrideIfSet(b.crit, def.dangerLevel);
        overrideIfSet(b.highWarn, def.highWarningLevel);
        overrideIfSet(b.highCrit, def.highDangerLevel);
        return;
    }
}

constexpr int kWholeDecimals = 0;
constexpr int kHundredthsDecimals = 2;

struct FixedText {
    char text[16];
};

FixedText fixed(float value, int decimals) {
    FixedText out = {};
    FloatFormat::formatFixed(out.text, sizeof(out.text), value, decimals);
    return out;
}

void logThresholds() {
    LOG_INFO("ALERT",
             "Alert engine initialized (revLimit=%s RPM, coolant warn=%s crit=%s, "
             "oilT warn=%s crit=%s, oilP warn=%s crit=%s, "
             "batt lowWarn=%s lowCrit=%s highWarn=%s highCrit=%s)",
             fixed(s_revLimitRpm, kWholeDecimals).text, fixed(s_coolantWarnC, kWholeDecimals).text,
             fixed(s_coolantCritC, kWholeDecimals).text, fixed(s_oilTempWarnC, kWholeDecimals).text,
             fixed(s_oilTempCritC, kWholeDecimals).text,
             fixed(s_oilPressWarnBar, kHundredthsDecimals).text,
             fixed(s_oilPressCritBar, kHundredthsDecimals).text,
             fixed(s_batteryLowWarnV, kHundredthsDecimals).text,
             fixed(s_batteryLowCritV, kHundredthsDecimals).text,
             fixed(s_batteryHighWarnV, kHundredthsDecimals).text,
             fixed(s_batteryHighCritV, kHundredthsDecimals).text);
}

AlertEngine::AlertLevel stepBattery(float volts, uint32_t now) {
    return static_cast<AlertEngine::AlertLevel>(alert_battery_step_rs(
        &s_batteryHold, volts, s_batteryLowWarnV, s_batteryLowCritV, s_batteryHighWarnV,
        s_batteryHighCritV, now, kAlertHysteresisPct, kAlertMinActiveMs));
}

} // namespace

void AlertEngine::init() {
    s_state = {};
    s_revLimiterSinceMs = 0;
    s_revLimiterEngaged = false;
    for (SensorHealthSlot &slot : s_sensorHealth)
        slot.health = {};
    s_coolantHold = {};
    s_oilTempHold = {};
    s_oilPressureHold = {};
    s_batteryHold = {};

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    s_revLimitRpm = dash.loaded && dash.revLimitRpm > 0.0f ? dash.revLimitRpm : kDefaultRevLimitRpm;
    CriticalAlertSource::init(dash.criticalAlerts, dash.criticalAlertCount);

    resetSignalBindings();
    const CfgSignalConfig &sigCfg = ConfigLoader::getSignalConfig();
    for (uint8_t i = 0; i < sigCfg.signalCount; i++) {
        bindDeclaredTemp(sigCfg.signals[i]);
        bindThresholds(sigCfg.signals[i]);
    }

    logThresholds();
    for (const DeclaredTempSource &src : s_declaredTemps) {
        if (!src.declared) {
            LOG_INFO("ALERT", "%s not in the signal catalogue — no alert armed", src.name);
            continue;
        }
        LOG_INFO("ALERT", "%s armed (warn=%s crit=%s)", src.name,
                 fixed(src.limits.warnC, kWholeDecimals).text,
                 fixed(src.limits.critC, kWholeDecimals).text);
    }
}

void AlertEngine::tick(const SignalStore::SignalValue *snap) {
    const uint32_t now = millis();
    updateSensorHealth(snap, now);

    const auto readSnap = [&snap](SignalId id, float def) {
        return snap[id].valid ? snap[id].raw : def;
    };
    const float rpm = readSnap(SignalIds::kRpm, 0.0f);
    float coolant = readSnap(SignalIds::kCoolantTempC, 0.0f);
    float oilTemp = readSnap(SignalIds::kOilTempC, 0.0f);
    float oilPres = readSnap(SignalIds::kOilPressBar, 5.0f);
    float volts = readSnap(SignalIds::kBatteryVolts, 13.0f);
    float mil = readSnap(SignalIds::kFlagMil, 0.0f);

    s_state.revLimiter = evalRevLimiter(rpm);
    s_state.coolantTemp = withSensorLost(stepCoolantTemp(coolant, now), s_state.coolantSensorLost);
    s_state.oilTemp = withSensorLost(stepOilTemp(oilTemp, now), s_state.oilTempSensorLost);
    s_state.oilPressure =
        withSensorLost(stepOilPressure(oilPres, now), s_state.oilPressureSensorLost);
    s_state.milActive = (mil > 0.5f);
    s_state.batteryVoltage = withSensorLost(stepBattery(volts, now), s_state.batterySensorLost);
    stepDeclaredTemps(snap, now);
    stepCriticalAlerts(snap, now);

    s_state.global = s_state.revLimiter;
    s_state.global = maxLevel(s_state.global, s_state.coolantTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilPressure);
    s_state.global = maxLevel(s_state.global, s_state.batteryVoltage);
    for (const DeclaredTempSource &src : s_declaredTemps)
        s_state.global = maxLevel(s_state.global, s_state.*(src.level));
    for (const AlertLevel level : s_state.userCritical)
        s_state.global = maxLevel(s_state.global, level);

    stepRevLimitBlink(s_state.revLimiter == AlertLevel::CRITICAL, now);
}

AlertEngine::AlertState AlertEngine::getState() {
    return s_state;
}

AlertEngine::CriticalLimit AlertEngine::criticalLimitFor(SignalId id, float reading) {
    for (const DeclaredTempSource &src : s_declaredTemps) {
        if (src.id != id || !src.declared)
            continue;
        AlertCrossedLimitRs out = {};
        alert_crossed_limit_rs(&out, reading, src.limits.critC, false, NAN);
        return {out.limit, out.below, out.valid};
    }
    for (const CriticalLimitBinding &b : kCriticalLimits) {
        if (b.id != id)
            continue;
        AlertCrossedLimitRs out = {};
        alert_crossed_limit_rs(&out, reading, *b.primary, b.primaryBelow, *b.highCrit);
        return {out.limit, out.below, out.valid};
    }
    return {0.0f, false, false};
}

bool AlertEngine::isRevLimiterRowLit() {
    return s_state.revLimiterRowLit;
}
