// alert_engine.cpp — Warning and alert state machine implementation

#include "alert_engine.h"
#include "signal_store.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "config/config_types.h"
#include "app_config.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <cmath>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static AlertEngine::AlertState s_state = {};
static uint32_t s_lastFlashToggleMs = 0;
static bool s_flashPhase = false;

// Rev limiter RPM (loaded from dashboard.json at boot)
static float s_revLimitRpm = 7200.0f;

// Signal thresholds — loaded from signals.json at init(), with compile-time fallbacks
static float s_coolantWarnC      = 100.0f;
static float s_coolantCritC      = 110.0f;
static float s_oilTempWarnC      = 120.0f;
static float s_oilTempCritC      = 135.0f;
static float s_oilPressWarnBar   = 1.5f;
static float s_oilPressCritBar   = 1.0f;
static float s_batteryLowWarnV   = BATTERY_DEFAULT_LOW_WARN_V;
static float s_batteryLowCritV   = BATTERY_DEFAULT_LOW_CRIT_V;
static float s_batteryHighWarnV  = BATTERY_DEFAULT_HIGH_WARN_V;

// Flash period in milliseconds
static constexpr uint32_t FLASH_PERIOD_MS = 1000 / (ALERT_REVLIMIT_FLASH_HZ * 2);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

AlertEngine::AlertLevel evalRevLimiter(float rpm) {
    float warnRpm  = s_revLimitRpm * (ALERT_REVLIMIT_WARN_PCT  / 100.0f);
    float flashRpm = s_revLimitRpm * (ALERT_REVLIMIT_FLASH_PCT / 100.0f);

    if (rpm >= flashRpm)
        return AlertEngine::AlertLevel::CRITICAL;
    if (rpm >= warnRpm)
        return AlertEngine::AlertLevel::WARNING;
    return AlertEngine::AlertLevel::NORMAL;
}

AlertEngine::AlertLevel evalCoolantTemp(float tempC) {
    if (tempC >= s_coolantCritC)
        return AlertEngine::AlertLevel::CRITICAL;
    if (tempC >= s_coolantWarnC)
        return AlertEngine::AlertLevel::WARNING;
    if (tempC >= s_coolantWarnC - 5.0f)
        return AlertEngine::AlertLevel::CAUTION;
    return AlertEngine::AlertLevel::NORMAL;
}

AlertEngine::AlertLevel evalOilTemp(float tempC) {
    if (tempC >= s_oilTempCritC)
        return AlertEngine::AlertLevel::CRITICAL;
    if (tempC >= s_oilTempWarnC)
        return AlertEngine::AlertLevel::WARNING;
    return AlertEngine::AlertLevel::NORMAL;
}

AlertEngine::AlertLevel evalOilPressure(float pressBar) {
    // Low oil pressure is dangerous — alert on LOW values
    if (pressBar <= s_oilPressCritBar)
        return AlertEngine::AlertLevel::CRITICAL;
    if (pressBar <= s_oilPressWarnBar)
        return AlertEngine::AlertLevel::WARNING;
    return AlertEngine::AlertLevel::NORMAL;
}

AlertEngine::AlertLevel evalBattery(float volts) {
    if (volts < s_batteryLowCritV)
        return AlertEngine::AlertLevel::CRITICAL;
    if (volts < s_batteryLowWarnV || volts > s_batteryHighWarnV)
        return AlertEngine::AlertLevel::WARNING;
    return AlertEngine::AlertLevel::NORMAL;
}

AlertEngine::AlertLevel maxLevel(AlertEngine::AlertLevel a, AlertEngine::AlertLevel b) {
    return (static_cast<uint8_t>(a) > static_cast<uint8_t>(b)) ? a : b;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void AlertEngine::init() {
    s_state = {};
    s_lastFlashToggleMs = 0;
    s_flashPhase = false;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (dash.loaded && dash.revLimitRpm > 0.0f)
        s_revLimitRpm = dash.revLimitRpm;

    // Load alert thresholds from signals.json — overrides compile-time fallbacks
    const CfgSignalConfig &sigCfg = ConfigLoader::getSignalConfig();
    for (uint8_t i = 0; i < sigCfg.signalCount; i++) {
        const CfgSignalDef &def = sigCfg.signals[i];
        if (strcmp(def.name, "coolant_temp_c") == 0) {
            if (!isnan(def.warningLevel)) s_coolantWarnC    = def.warningLevel;
            if (!isnan(def.dangerLevel))  s_coolantCritC    = def.dangerLevel;
        } else if (strcmp(def.name, "oil_temp_c") == 0) {
            if (!isnan(def.warningLevel)) s_oilTempWarnC    = def.warningLevel;
            if (!isnan(def.dangerLevel))  s_oilTempCritC    = def.dangerLevel;
        } else if (strcmp(def.name, "oil_press_bar") == 0) {
            // Low-side alert: warningLevel = warn-below, dangerLevel = crit-below
            if (!isnan(def.warningLevel)) s_oilPressWarnBar = def.warningLevel;
            if (!isnan(def.dangerLevel))  s_oilPressCritBar = def.dangerLevel;
        } else if (strcmp(def.name, "battery_volts") == 0) {
            // Battery has both LOW and HIGH thresholds:
            //   warningLevel     -> low-warn (below = battery weak)
            //   dangerLevel      -> low-crit (below = will not crank)
            //   highWarningLevel -> high-warn (above = charging fault / overvoltage)
            if (!isnan(def.warningLevel))     s_batteryLowWarnV  = def.warningLevel;
            if (!isnan(def.dangerLevel))      s_batteryLowCritV  = def.dangerLevel;
            if (!isnan(def.highWarningLevel)) s_batteryHighWarnV = def.highWarningLevel;
        }
    }

    LOG_INFO("ALERT", "Alert engine initialized (revLimit=%.0f RPM, coolant warn=%.0f crit=%.0f, "
             "oilT warn=%.0f crit=%.0f, oilP warn=%.2f crit=%.2f, "
             "batt lowWarn=%.2f lowCrit=%.2f highWarn=%.2f)",
             s_revLimitRpm, s_coolantWarnC, s_coolantCritC,
             s_oilTempWarnC, s_oilTempCritC, s_oilPressWarnBar, s_oilPressCritBar,
             s_batteryLowWarnV, s_batteryLowCritV, s_batteryHighWarnV);
}

void AlertEngine::tick() {
    float rpm     = SignalStore::read(SignalIds::RPM,           0.0f);
    float coolant = SignalStore::read(SignalIds::COOLANT_TEMP_C, 0.0f);
    float oilTemp = SignalStore::read(SignalIds::OIL_TEMP_C,    0.0f);
    float oilPres = SignalStore::read(SignalIds::OIL_PRESS_BAR, 5.0f);
    float volts   = SignalStore::read(SignalIds::BATTERY_VOLTS, 13.0f);
    float mil     = SignalStore::read(SignalIds::FLAG_MIL,       0.0f);

    s_state.revLimiter    = evalRevLimiter(rpm);
    s_state.coolantTemp   = evalCoolantTemp(coolant);
    s_state.oilTemp       = evalOilTemp(oilTemp);
    s_state.oilPressure   = evalOilPressure(oilPres);
    s_state.milActive     = (mil > 0.5f);
    s_state.batteryVoltage = evalBattery(volts);

    s_state.global = s_state.revLimiter;
    s_state.global = maxLevel(s_state.global, s_state.coolantTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilPressure);
    s_state.global = maxLevel(s_state.global, s_state.batteryVoltage);

    bool revCritical = (s_state.revLimiter == AlertLevel::CRITICAL);
    if (revCritical) {
        uint32_t now = millis();
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
