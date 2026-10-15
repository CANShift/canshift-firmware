#include "critical_alert_source.h"

#include "alert_engine_rs.h"
#include "app_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <cmath>
#include <cstdlib>

namespace {

CriticalAlertSource::Armed s_armed = {};
AlertLevelHoldRs s_hold = {};
AlertSensorHealthRs s_health = {};

bool resolveSignal(const CfgCriticalAlert &cfg, SignalId *out) {
    *out = signalIdFromName(cfg.signalId);
    if (*out < SignalIds::kSignalCount)
        return true;
    LOG_ERROR("ALERT", "critical alert signal '%s' is not one the dash reads — not armed",
              cfg.signalId);
    ErrorStore::push(ERROR_SRC_CONFIG, "ALERT_SIG", "critical alert signal not readable");
    return false;
}

} // namespace

void CriticalAlertSource::init(const CfgCriticalAlert &cfg) {
    s_armed = {};
    s_hold = {};
    s_health = {};
    if (!cfg.armed)
        return;
    SignalId id = SignalIds::kSignalCount;
    if (!resolveSignal(cfg, &id))
        return;
    s_armed = {true, id, cfg.firesAt, cfg.below};
    const long firesAtX100 = labs(lroundf(cfg.firesAt * 100.0f));
    LOG_INFO("ALERT", "critical alert armed on '%s' (%s %s%ld.%02ld)", cfg.signalId,
             cfg.below ? "below" : "above", cfg.firesAt < 0.0f ? "-" : "", firesAtX100 / 100,
             firesAtX100 % 100);
}

AlertEngine::AlertLevel CriticalAlertSource::step(const SignalStore::SignalValue *snap,
                                                  uint32_t now, bool *sensorLost) {
    *sensorLost = false;
    if (!s_armed.armed)
        return AlertEngine::AlertLevel::NORMAL;
    const SignalStore::SignalValue &reading = snap[s_armed.id];
    alert_sensor_health_step_rs(&s_health, reading.valid, now, kAlertSensorLostClearHoldMs);
    *sensorLost = s_health.lost;
    if (!reading.valid)
        return AlertEngine::AlertLevel::NORMAL;
    return static_cast<AlertEngine::AlertLevel>(
        alert_critical_alert_step_rs(&s_hold, reading.smoothed, s_armed.firesAt, s_armed.below, now,
                                     kAlertHysteresisPct, kAlertMinActiveMs));
}

CriticalAlertSource::Armed CriticalAlertSource::armed() {
    return s_armed;
}
