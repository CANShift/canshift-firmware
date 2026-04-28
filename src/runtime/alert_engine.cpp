// alert_engine.cpp — Warning and alert state machine implementation

#include "alert_engine.h"
#include "signal_store.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "app_config.h"
#include "diag/logger.h"

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static AlertEngine::AlertState s_state = {};
static uint32_t s_lastFlashToggleMs    = 0;
static bool     s_flashPhase           = false;

// Rev limiter RPM (loaded from config at boot)
static float s_revLimitRpm = 7200.0f; // VR6 2.9 typical — TODO: load from config

// Flash period in milliseconds
static constexpr uint32_t FLASH_PERIOD_MS = 1000 / (ALERT_REVLIMIT_FLASH_HZ * 2);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

    AlertEngine::AlertLevel evalRevLimiter(float rpm) {
        float warnRpm  = s_revLimitRpm * (ALERT_REVLIMIT_WARN_PCT  / 100.0f);
        float flashRpm = s_revLimitRpm * (ALERT_REVLIMIT_FLASH_PCT / 100.0f);

        if (rpm >= flashRpm) return AlertEngine::AlertLevel::CRITICAL;
        if (rpm >= warnRpm)  return AlertEngine::AlertLevel::WARNING;
        return AlertEngine::AlertLevel::NORMAL;
    }

    AlertEngine::AlertLevel evalCoolantTemp(float tempC) {
        if (tempC >= ALERT_COOLANT_CRIT_C) return AlertEngine::AlertLevel::CRITICAL;
        if (tempC >= ALERT_COOLANT_WARN_C) return AlertEngine::AlertLevel::WARNING;
        if (tempC >= ALERT_COOLANT_WARN_C - 5) return AlertEngine::AlertLevel::CAUTION;
        return AlertEngine::AlertLevel::NORMAL;
    }

    AlertEngine::AlertLevel evalOilTemp(float tempC) {
        if (tempC >= ALERT_OIL_TEMP_CRIT_C) return AlertEngine::AlertLevel::CRITICAL;
        if (tempC >= ALERT_OIL_TEMP_WARN_C) return AlertEngine::AlertLevel::WARNING;
        return AlertEngine::AlertLevel::NORMAL;
    }

    AlertEngine::AlertLevel evalOilPressure(float pressBar) {
        // Low oil pressure is dangerous — alert on LOW values
        if (pressBar <= ALERT_OIL_PRESS_CRIT_BAR) return AlertEngine::AlertLevel::CRITICAL;
        if (pressBar <= ALERT_OIL_PRESS_WARN_BAR) return AlertEngine::AlertLevel::WARNING;
        return AlertEngine::AlertLevel::NORMAL;
    }

    AlertEngine::AlertLevel evalBattery(float volts) {
        if (volts < 11.5f) return AlertEngine::AlertLevel::CRITICAL;
        if (volts < 12.0f) return AlertEngine::AlertLevel::WARNING;
        if (volts > 15.0f) return AlertEngine::AlertLevel::WARNING;  // Overcharge
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
    s_state         = {};
    s_lastFlashToggleMs = 0;
    s_flashPhase    = false;

    // TODO: Load rev limit from dashboard config
    // s_revLimitRpm = ConfigLoader::getDashboardConfig().revLimitRpm;

    LOG_INFO("ALERT", "Alert engine initialized (revLimit=%.0f RPM)", s_revLimitRpm);
}

void AlertEngine::tick() {
    float rpm     = SignalStore::read(SignalIds::RPM,          0.0f);
    float coolant = SignalStore::read(SignalIds::COOLANT_TEMP_C, 0.0f);
    float oilTemp = SignalStore::read(SignalIds::OIL_TEMP_C,   0.0f);
    float oilPres = SignalStore::read(SignalIds::OIL_PRESS_BAR, 5.0f);
    float volts   = SignalStore::read(SignalIds::BATTERY_VOLTS, 13.0f);
    float mil     = SignalStore::read(SignalIds::FLAG_MIL,     0.0f);

    s_state.revLimiter    = evalRevLimiter(rpm);
    s_state.coolantTemp   = evalCoolantTemp(coolant);
    s_state.oilTemp       = evalOilTemp(oilTemp);
    s_state.oilPressure   = evalOilPressure(oilPres);
    s_state.milActive     = (mil > 0.5f);
    s_state.batteryVoltage = evalBattery(volts);

    // Global = highest of all individual levels
    s_state.global = s_state.revLimiter;
    s_state.global = maxLevel(s_state.global, s_state.coolantTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilTemp);
    s_state.global = maxLevel(s_state.global, s_state.oilPressure);
    s_state.global = maxLevel(s_state.global, s_state.batteryVoltage);

    // Rev limiter flash
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
    if (s_state.revLimiterFlashActive) {
        return 0xFF0000;  // LVGL hex color: red
    }
    return 0x000000;  // Black = transparent overlay (will be hidden by opacity)
}
