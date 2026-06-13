#pragma once

#include "config_types.h"

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

[[nodiscard]] bool reloadAll();

[[nodiscard]] const CfgSignalDef *findSignal(const char *name);

} // namespace ConfigLoader
