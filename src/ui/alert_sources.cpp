#include "alert_sources.h"

#include "config/config_loader.h"
#include "runtime/critical_alert_source.h"
#include "ui/widget_label.h"

#include <cmath>
#include <string.h>

namespace {

constexpr float kWholeNumbersFrom = 10.0f;
constexpr size_t kUnitLen = 16;

char s_userName[WidgetLabelOverlay::kKickerBufLen] = "";
char s_userUnit[kUnitLen] = "";
AlertSources::CriticalSource s_user = {};

AlertEngine::CriticalLimit userLimitFor(SignalId, float) {
    const CriticalAlertSource::Armed armed = CriticalAlertSource::armed();
    return {armed.firesAt, armed.below, armed.armed};
}

int decimalsFor(float firesAt) {
    return fabsf(firesAt) < kWholeNumbersFrom ? 1 : 0;
}

const AlertSources::CriticalSource &userSource(const CriticalAlertSource::Armed &armed) {
    const char *signalName = ConfigLoader::getDashboardConfig().criticalAlert.signalId;
    WidgetLabelOverlay::signalKicker(signalName, s_userName, sizeof(s_userName));
    const CfgSignalDef *def = ConfigLoader::findSignal(signalName);
    strlcpy(s_userUnit, def ? def->unit : "", sizeof(s_userUnit));
    s_user = {s_userName,
              s_userName,
              armed.id,
              signalName,
              s_userUnit,
              decimalsFor(armed.firesAt),
              &AlertEngine::AlertState::userCritical,
              &AlertEngine::AlertState::userSensorLost,
              userLimitFor};
    return s_user;
}

} // namespace

size_t AlertSources::count() {
    return kSourceCount + (CriticalAlertSource::armed().armed ? 1 : 0);
}

const AlertSources::CriticalSource &AlertSources::at(size_t index) {
    if (index < kSourceCount)
        return kSources[index];
    return userSource(CriticalAlertSource::armed());
}
