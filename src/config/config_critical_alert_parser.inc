#include "config_loader_internal.h"

#include "diag/error_store.h"
#include "diag/logger.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

namespace ConfigLoaderInternal {

namespace {

constexpr const char *kDirectionBelow = "below";

bool readCriticalAlert(JsonObjectConst src, CfgCriticalAlert *out) {
    const char *signal = src["signal"] | "";
    JsonVariantConst firesAt = src["firesAt"];
    if (signal[0] == '\0' || !firesAt.is<float>())
        return false;
    if (strlcpy(out->signalId, signal, sizeof(out->signalId)) >= sizeof(out->signalId))
        return false;
    out->firesAt = firesAt.as<float>();
    if (!std::isfinite(out->firesAt))
        return false;
    out->below = strcmp(src["direction"] | "", kDirectionBelow) == 0;
    return true;
}

void parseCriticalAlert(JsonObjectConst src, uint8_t index, CfgCriticalAlert *out) {
    *out = CfgCriticalAlert{};
    out->firesAt = NAN;
    out->armed = readCriticalAlert(src, out);
    if (out->armed)
        return;
    LOG_ERROR("CFG", "dashboard.json: criticalAlerts[%u] incomplete — not armed",
              static_cast<unsigned>(index));
    ErrorStore::push(ERROR_SRC_CONFIG, "ALERT_CFG", "critical alert incomplete");
}

void parseCriticalAlertList(JsonArrayConst list, CfgDashboard *out) {
    if (list.size() > static_cast<size_t>(kConfigMaxCriticalAlerts)) {
        LOG_ERROR("CFG", "dashboard.json: %u critical alerts — only the first %d are armed",
                  static_cast<unsigned>(list.size()), kConfigMaxCriticalAlerts);
        ErrorStore::push(ERROR_SRC_CONFIG, "ALERT_CFG", "too many critical alerts");
    }
    for (JsonObjectConst entry : list) {
        if (out->criticalAlertCount >= kConfigMaxCriticalAlerts)
            return;
        const uint8_t index = out->criticalAlertCount++;
        parseCriticalAlert(entry, index, &out->criticalAlerts[index]);
    }
}

}

void parseCriticalAlerts(const JsonDocument &doc, CfgDashboard *out) {
    out->criticalAlertCount = 0;
    JsonArrayConst list = doc["criticalAlerts"];
    if (!list.isNull()) {
        parseCriticalAlertList(list, out);
        return;
    }
    JsonObjectConst legacy = doc["criticalAlert"];
    if (legacy.isNull())
        return;
    LOG_INFO("CFG", "dashboard.json: legacy criticalAlert read as a one-entry list");
    out->criticalAlertCount = 1;
    parseCriticalAlert(legacy, 0, &out->criticalAlerts[0]);
}

}
