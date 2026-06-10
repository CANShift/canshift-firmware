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

#if USE_RUST_ALERT_ENGINE
    #include "alert_engine_rs.h"
#endif

static AlertEngine::AlertState s_state = {};
static uint32_t s_lastFlashToggleMs = 0;
static bool s_flashPhase = false;

static float s_revLimitRpm = 7200.0f;

// warning/danger = low-side for battery/oil-press, high-side for temps.
// highWarning/highDanger = optional additional high-side; NaN = disabled.
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

AlertEngine::AlertLevel maxLevel(AlertEngine::AlertLevel a, AlertEngine::AlertLevel b) {
    return (static_cast<uint8_t>(a) > static_cast<uint8_t>(b)) ? a : b;
}

// NaN = disabled. Returns NORMAL when neither threshold is configured so
// callers can safely max() with their existing per-signal verdict.
AlertEngine::AlertLevel evalHighSide(float value, float highWarn, float highCrit) {
#if USE_RUST_ALERT_ENGINE
    return static_cast<AlertEngine::AlertLevel>(alert_eval_high_side_rs(value, highWarn, highCrit));
#else
    if (!isnan(highCrit) && value > highCrit)
        return AlertEngine::AlertLevel::CRITICAL;
    if (!isnan(highWarn) && value > highWarn)
        return AlertEngine::AlertLevel::WARNING;
    return AlertEngine::AlertLevel::NORMAL;
#endif
}

AlertEngine::AlertLevel evalRevLimiter(float rpm) {
#if USE_RUST_ALERT_ENGINE
    return static_cast<AlertEngine::AlertLevel>(alert_eval_rev_limiter_rs(
        rpm, s_revLimitRpm, ALERT_REVLIMIT_WARN_PCT, ALERT_REVLIMIT_FLASH_PCT));
#else
    float warnRpm = s_revLimitRpm * (ALERT_REVLIMIT_WARN_PCT / 100.0f);
    float flashRpm = s_revLimitRpm * (ALERT_REVLIMIT_FLASH_PCT / 100.0f);

    if (rpm >= flashRpm)
        return AlertEngine::AlertLevel::CRITICAL;
    if (rpm >= warnRpm)
        return AlertEngine::AlertLevel::WARNING;
    return AlertEngine::AlertLevel::NORMAL;
#endif
}

AlertEngine::AlertLevel evalCoolantTemp(float tempC) {
#if USE_RUST_ALERT_ENGINE
    return static_cast<AlertEngine::AlertLevel>(alert_eval_coolant_temp_rs(
        tempC, s_coolantWarnC, s_coolantCritC, s_coolantHighWarnC, s_coolantHighCritC));
#else
    AlertEngine::AlertLevel base;
    if (tempC >= s_coolantCritC)
        base = AlertEngine::AlertLevel::CRITICAL;
    else if (tempC >= s_coolantWarnC)
        base = AlertEngine::AlertLevel::WARNING;
    else if (tempC >= s_coolantWarnC - 5.0f)
        base = AlertEngine::AlertLevel::CAUTION;
    else
        base = AlertEngine::AlertLevel::NORMAL;
    return maxLevel(base, evalHighSide(tempC, s_coolantHighWarnC, s_coolantHighCritC));
#endif
}

AlertEngine::AlertLevel evalOilTemp(float tempC) {
#if USE_RUST_ALERT_ENGINE
    return static_cast<AlertEngine::AlertLevel>(alert_eval_oil_temp_rs(
        tempC, s_oilTempWarnC, s_oilTempCritC, s_oilTempHighWarnC, s_oilTempHighCritC));
#else
    AlertEngine::AlertLevel base;
    if (tempC >= s_oilTempCritC)
        base = AlertEngine::AlertLevel::CRITICAL;
    else if (tempC >= s_oilTempWarnC)
        base = AlertEngine::AlertLevel::WARNING;
    else
        base = AlertEngine::AlertLevel::NORMAL;
    return maxLevel(base, evalHighSide(tempC, s_oilTempHighWarnC, s_oilTempHighCritC));
#endif
}

AlertEngine::AlertLevel evalOilPressure(float pressBar) {
#if USE_RUST_ALERT_ENGINE
    return static_cast<AlertEngine::AlertLevel>(
        alert_eval_oil_pressure_rs(pressBar, s_oilPressWarnBar, s_oilPressCritBar,
                                   s_oilPressHighWarnBar, s_oilPressHighCritBar));
#else
    // Low oil pressure is dangerous — alert on LOW values via warningLevel/dangerLevel.
    // Optional high-side via highWarningLevel/highDangerLevel (rare: over-pressure).
    AlertEngine::AlertLevel base;
    if (pressBar <= s_oilPressCritBar)
        base = AlertEngine::AlertLevel::CRITICAL;
    else if (pressBar <= s_oilPressWarnBar)
        base = AlertEngine::AlertLevel::WARNING;
    else
        base = AlertEngine::AlertLevel::NORMAL;
    return maxLevel(base, evalHighSide(pressBar, s_oilPressHighWarnBar, s_oilPressHighCritBar));
#endif
}

AlertEngine::AlertLevel evalBattery(float volts) {
#if USE_RUST_ALERT_ENGINE
    return static_cast<AlertEngine::AlertLevel>(alert_eval_battery_rs(
        volts, s_batteryLowWarnV, s_batteryLowCritV, s_batteryHighWarnV, s_batteryHighCritV));
#else
    // Low side first (cranking failure dominates).
    if (volts < s_batteryLowCritV)
        return AlertEngine::AlertLevel::CRITICAL;
    AlertEngine::AlertLevel base = (volts < s_batteryLowWarnV) ? AlertEngine::AlertLevel::WARNING
                                                               : AlertEngine::AlertLevel::NORMAL;
    return maxLevel(base, evalHighSide(volts, s_batteryHighWarnV, s_batteryHighCritV));
#endif
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

    // Load alert thresholds from signals.json — overrides compile-time fallbacks.
    // Every signal evaluated below also honors highWarningLevel / highDangerLevel
    // when set (NaN = disabled), in addition to the legacy warningLevel/dangerLevel.
    const CfgSignalConfig &sigCfg = ConfigLoader::getSignalConfig();
    for (uint8_t i = 0; i < sigCfg.signalCount; i++) {
        const CfgSignalDef &def = sigCfg.signals[i];
        if (strcmp(def.name, "coolant_temp_c") == 0) {
            if (!isnan(def.warningLevel))
                s_coolantWarnC = def.warningLevel;
            if (!isnan(def.dangerLevel))
                s_coolantCritC = def.dangerLevel;
            if (!isnan(def.highWarningLevel))
                s_coolantHighWarnC = def.highWarningLevel;
            if (!isnan(def.highDangerLevel))
                s_coolantHighCritC = def.highDangerLevel;
        } else if (strcmp(def.name, "oil_temp_c") == 0) {
            if (!isnan(def.warningLevel))
                s_oilTempWarnC = def.warningLevel;
            if (!isnan(def.dangerLevel))
                s_oilTempCritC = def.dangerLevel;
            if (!isnan(def.highWarningLevel))
                s_oilTempHighWarnC = def.highWarningLevel;
            if (!isnan(def.highDangerLevel))
                s_oilTempHighCritC = def.highDangerLevel;
        } else if (strcmp(def.name, "oil_press_bar") == 0) {
            // Low-side alert: warningLevel = warn-below, dangerLevel = crit-below.
            // Optional high-side (over-pressure) via highWarningLevel/highDangerLevel.
            if (!isnan(def.warningLevel))
                s_oilPressWarnBar = def.warningLevel;
            if (!isnan(def.dangerLevel))
                s_oilPressCritBar = def.dangerLevel;
            if (!isnan(def.highWarningLevel))
                s_oilPressHighWarnBar = def.highWarningLevel;
            if (!isnan(def.highDangerLevel))
                s_oilPressHighCritBar = def.highDangerLevel;
        } else if (strcmp(def.name, "battery_volts") == 0) {
            // Battery has BOTH low- and high-side thresholds:
            //   warningLevel     -> low-warn (below = battery weak)
            //   dangerLevel      -> low-crit (below = will not crank)
            //   highWarningLevel -> high-warn (above = charging fault / overvoltage)
            //   highDangerLevel  -> high-crit (above = regulator failure)
            if (!isnan(def.warningLevel))
                s_batteryLowWarnV = def.warningLevel;
            if (!isnan(def.dangerLevel))
                s_batteryLowCritV = def.dangerLevel;
            if (!isnan(def.highWarningLevel))
                s_batteryHighWarnV = def.highWarningLevel;
            if (!isnan(def.highDangerLevel))
                s_batteryHighCritV = def.highDangerLevel;
        }
    }

    // Format integer + 2-decimal-fixed-point manually so this log doesn't
    // pull newlib's float printf into the link (#405). `xN` suffix = value
    // scaled by 100 for `%d.%02d` rendering.
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
    float rpm = SignalStore::read(SignalIds::RPM, 0.0f);
    float coolant = SignalStore::read(SignalIds::COOLANT_TEMP_C, 0.0f);
    float oilTemp = SignalStore::read(SignalIds::OIL_TEMP_C, 0.0f);
    float oilPres = SignalStore::read(SignalIds::OIL_PRESS_BAR, 5.0f);
    float volts = SignalStore::read(SignalIds::BATTERY_VOLTS, 13.0f);
    float mil = SignalStore::read(SignalIds::FLAG_MIL, 0.0f);

    s_state.revLimiter = evalRevLimiter(rpm);
    s_state.coolantTemp = evalCoolantTemp(coolant);
    s_state.oilTemp = evalOilTemp(oilTemp);
    s_state.oilPressure = evalOilPressure(oilPres);
    s_state.milActive = (mil > 0.5f);
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
