#include "alert_sources.h"

#include "config/config_loader.h"
#include "runtime/critical_alert_source.h"
#include "ui/widget_label.h"

#include <cmath>
#include <string.h>

namespace {

constexpr float kWholeNumbersFrom = 10.0f;
constexpr size_t kUnitLen = 16;

struct ConfiguredSource {
    char name[WidgetLabelOverlay::kKickerBufLen];
    char unit[kUnitLen];
    AlertSources::CriticalSource source;
};

ConfiguredSource s_configured[kConfigMaxCriticalAlerts] = {};

int decimalsFor(float firesAt) {
    return fabsf(firesAt) < kWholeNumbersFrom ? 1 : 0;
}

const AlertSources::CriticalSource &configuredSource(size_t slot) {
    const CriticalAlertSource::Armed armed = CriticalAlertSource::armed(slot);
    const char *signalName = ConfigLoader::getDashboardConfig().criticalAlerts[slot].signalId;
    ConfiguredSource &out = s_configured[slot];
    WidgetLabelOverlay::signalKicker(signalName, out.name, sizeof(out.name));
    const CfgSignalDef *def = ConfigLoader::findSignal(signalName);
    strlcpy(out.unit, def ? def->unit : "", sizeof(out.unit));
    out.source = {out.name, out.name, armed.id, signalName, out.unit, decimalsFor(armed.firesAt)};
    return out.source;
}

bool isConfigured(size_t index) {
    return index >= AlertSources::kSourceCount && index < AlertSources::count();
}

}

size_t AlertSources::count() {
    return kSourceCount + CriticalAlertSource::count();
}

const AlertSources::CriticalSource &AlertSources::at(size_t index) {
    if (index < kSourceCount)
        return kSources[index].info;
    return configuredSource(index - kSourceCount);
}

AlertEngine::AlertLevel AlertSources::levelAt(const AlertEngine::AlertState &state, size_t index) {
    if (index < kSourceCount)
        return state.*(kSources[index].level);
    if (!isConfigured(index))
        return AlertEngine::AlertLevel::NORMAL;
    return state.userCritical[index - kSourceCount];
}

bool AlertSources::sensorLostAt(const AlertEngine::AlertState &state, size_t index) {
    if (index < kSourceCount)
        return state.*(kSources[index].sensorLost);
    return isConfigured(index) && state.userSensorLost[index - kSourceCount];
}

AlertEngine::CriticalLimit AlertSources::limitAt(size_t index, float reading) {
    if (index < kSourceCount)
        return AlertEngine::criticalLimitFor(kSources[index].info.id, reading);
    const CriticalAlertSource::Armed armed = CriticalAlertSource::armed(index - kSourceCount);
    return {armed.firesAt, armed.below, armed.armed};
}
