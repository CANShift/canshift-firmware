#pragma once

#include "config_rejection.h"
#include "config_types.h"
#include "signals_source.h"

namespace ConfigLoader {

struct LoadResult {
    bool dashboardOk;
    bool signalsOk;
    bool deviceOk;
    bool inputsOk;
};

[[nodiscard]] LoadResult loadAll();

[[nodiscard]] const CfgDashboard &getDashboardConfig();

[[nodiscard]] const CfgSignalConfig &getSignalConfig();

[[nodiscard]] const CfgDeviceConfig &getDeviceConfig();

[[nodiscard]] const CfgInputBindings &getInputBindings();

[[nodiscard]] const CfgRejection &getRejection();

[[nodiscard]] bool reloadAll();

[[nodiscard]] const CfgSignalDef *findSignal(const char *name);

[[nodiscard]] SignalsSource::Verdict inspectSignalsFile(const char *path);

[[nodiscard]] bool acceptSignalsUpload(const char *stagedPath);

} // namespace ConfigLoader
