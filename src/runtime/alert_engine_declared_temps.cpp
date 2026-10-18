#include "alert_engine_internal.h"
#include "can/signal_map.h"
#include "app_config.h"
#include "diag/logger.h"
#include "alert_engine_rs.h"

#include <cmath>

using AlertEngineInternal::fixed;
using AlertEngineInternal::kWholeDecimals;
using AlertEngineInternal::maxLevel;
using AlertEngineInternal::withSensorLost;

namespace {

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

void stepSource(DeclaredTempSource &src, const SignalStore::SignalValue *snap, uint32_t now,
                AlertEngine::AlertState &state) {
    alert_sensor_health_step_rs(&src.health, snap[src.id].valid, now, kAlertSensorLostClearHoldMs);
    state.*(src.lost) = src.health.lost;
    const float value = snap[src.id].valid ? snap[src.id].raw : 0.0f;
    const auto level = static_cast<AlertEngine::AlertLevel>(
        alert_high_side_temp_step_rs(&src.hold, value, src.limits.warnC, src.limits.critC, NAN, NAN,
                                     now, kAlertHysteresisPct, kAlertMinActiveMs));
    state.*(src.level) = withSensorLost(level, src.health.lost);
}

}

namespace AlertDeclaredTemps {

void reset() {
    for (DeclaredTempSource &src : s_declaredTemps) {
        src.limits = src.defaults;
        src.declared = false;
        src.hold = {};
        src.health = {};
    }
}

void bind(const CfgSignalDef &def) {
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

void step(const SignalStore::SignalValue *snap, uint32_t now, AlertEngine::AlertState &state) {
    for (DeclaredTempSource &src : s_declaredTemps) {
        if (!src.declared)
            continue;
        stepSource(src, snap, now, state);
    }
}

AlertEngine::AlertLevel highest(const AlertEngine::AlertState &state,
                                AlertEngine::AlertLevel floor) {
    AlertEngine::AlertLevel level = floor;
    for (const DeclaredTempSource &src : s_declaredTemps)
        level = maxLevel(level, state.*(src.level));
    return level;
}

void logArming() {
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

bool criticalLimitFor(SignalId id, float reading, AlertEngine::CriticalLimit &out) {
    for (const DeclaredTempSource &src : s_declaredTemps) {
        if (src.id != id || !src.declared)
            continue;
        AlertCrossedLimitRs crossed = {};
        alert_crossed_limit_rs(&crossed, reading, src.limits.critC, false, NAN);
        out = {crossed.limit, crossed.below, crossed.valid};
        return true;
    }
    return false;
}

}
