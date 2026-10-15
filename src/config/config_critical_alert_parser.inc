
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
    out->below = strcmp(src["direction"] | "", kDirectionBelow) == 0;
    return true;
}

} // namespace

void parseCriticalAlert(JsonObjectConst src, CfgCriticalAlert *out) {
    *out = CfgCriticalAlert{};
    out->firesAt = NAN;
    if (src.isNull())
        return;
    out->armed = readCriticalAlert(src, out);
    if (out->armed)
        return;
    LOG_ERROR("CFG", "dashboard.json: criticalAlert incomplete — no user alert armed");
    ErrorStore::push(ERROR_SRC_CONFIG, "ALERT_CFG", "critical alert incomplete");
}

} // namespace ConfigLoaderInternal
