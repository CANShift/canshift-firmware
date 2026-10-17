#include "critical_alert_source.h"

#include "alert_engine_rs.h"
#include "app_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

struct Slot {
    CriticalAlertSource::Armed armed;
    AlertLevelHoldRs hold;
    AlertSensorHealthRs health;
};

Slot s_slots[kConfigMaxCriticalAlerts] = {};
size_t s_count = 0;

bool resolveSignal(const CfgCriticalAlert &cfg, SignalId *out) {
    *out = signalIdFromName(cfg.signalId);
    if (*out < SignalIds::kSignalCount)
        return true;
    LOG_ERROR("ALERT", "critical alert signal '%s' is not one the dash reads — not armed",
              cfg.signalId);
    ErrorStore::push(ERROR_SRC_CONFIG, "ALERT_SIG", "critical alert signal not readable");
    return false;
}

void arm(Slot &slot, const CfgCriticalAlert &cfg) {
    SignalId id = SignalIds::kSignalCount;
    if (!cfg.armed || !resolveSignal(cfg, &id))
        return;
    slot.armed = {true, id, cfg.firesAt, cfg.below};
    const long firesAtX100 = labs(lroundf(cfg.firesAt * 100.0f));
    LOG_INFO("ALERT", "critical alert armed on '%s' (%s %s%ld.%02ld)", cfg.signalId,
             cfg.below ? "below" : "above", cfg.firesAt < 0.0f ? "-" : "", firesAtX100 / 100,
             firesAtX100 % 100);
}

} // namespace

void CriticalAlertSource::init(const CfgCriticalAlert *alerts, size_t count) {
    for (Slot &slot : s_slots)
        slot = {};
    s_count =
        alerts == nullptr ? 0 : std::min(count, static_cast<size_t>(kConfigMaxCriticalAlerts));
    for (size_t i = 0; i < s_count; ++i)
        arm(s_slots[i], alerts[i]);
}

size_t CriticalAlertSource::count() {
    return s_count;
}

AlertEngine::AlertLevel CriticalAlertSource::step(size_t index,
                                                  const SignalStore::SignalValue *snap,
                                                  uint32_t now, bool *sensorLost) {
    *sensorLost = false;
    if (index >= s_count || !s_slots[index].armed.armed)
        return AlertEngine::AlertLevel::NORMAL;
    Slot &slot = s_slots[index];
    const SignalStore::SignalValue &reading = snap[slot.armed.id];
    alert_sensor_health_step_rs(&slot.health, reading.valid, now, kAlertSensorLostClearHoldMs);
    *sensorLost = slot.health.lost;
    if (!reading.valid)
        return AlertEngine::AlertLevel::NORMAL;
    return static_cast<AlertEngine::AlertLevel>(
        alert_critical_alert_step_rs(&slot.hold, reading.raw, slot.armed.firesAt, slot.armed.below,
                                     now, kAlertHysteresisPct, kAlertMinActiveMs));
}

CriticalAlertSource::Armed CriticalAlertSource::armed(size_t index) {
    if (index >= s_count)
        return {};
    return s_slots[index].armed;
}
