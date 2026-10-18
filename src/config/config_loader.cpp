#include "config_loader.h"

#include "app_config.h"
#include "config_loader_internal.h"
#include "diag/logger.h"
#include "layout_grid_rs.h"
#include "ui/screen_profile.h"

#include <cstdint>
#include <cstring>

namespace ConfigLoaderInternal {

CfgDashboard *s_dashboard = nullptr;
CfgSignalConfig s_signals = {};
CfgDeviceConfig s_device = {};
CfgInputBindings s_inputs = {};
CfgRejection s_rejection = {};

}

ConfigLoader::LoadResult ConfigLoader::loadAll() {
    LoadResult r{};
    r.dashboardOk = ConfigLoaderInternal::loadDashboard();
    r.signalsOk = ConfigLoaderInternal::loadSignals();
    r.deviceOk = ConfigLoaderInternal::loadDevice();

    r.inputsOk = ConfigLoaderInternal::loadInputBindings();
    return r;
}

const CfgDashboard &ConfigLoader::getDashboardConfig() {
    static const CfgDashboard kEmpty{};
    return ConfigLoaderInternal::s_dashboard ? *ConfigLoaderInternal::s_dashboard : kEmpty;
}
const CfgSignalConfig &ConfigLoader::getSignalConfig() {
    return ConfigLoaderInternal::s_signals;
}
const CfgDeviceConfig &ConfigLoader::getDeviceConfig() {
    return ConfigLoaderInternal::s_device;
}
const CfgInputBindings &ConfigLoader::getInputBindings() {
    return ConfigLoaderInternal::s_inputs;
}
const CfgRejection &ConfigLoader::getRejection() {
    return ConfigLoaderInternal::s_rejection;
}
bool ConfigLoader::reloadAll() {
    LoadResult r = loadAll();
    LOG_INFO("CFG", "Config reloaded: dashboard=%d signals=%d inputs=%d", r.dashboardOk,
             r.signalsOk, r.inputsOk);
    return r.dashboardOk;
}

const CfgSignalDef *ConfigLoader::findSignal(const char *name) {
    const CfgSignalConfig &signals = ConfigLoaderInternal::s_signals;
    if (!name || name[0] == '\0' || !signals.loaded)
        return nullptr;
    for (uint8_t i = 0; i < signals.signalCount; ++i) {
        if (strcmp(signals.signals[i].name, name) == 0) {
            return &signals.signals[i];
        }
    }
    return nullptr;
}

#include "config_dashboard_loader.inc"
#include "config_file_loaders.inc"
#include "config_layout_resolver.inc"
#include "config_value_widget_parser.inc"
#include "config_parser.inc"
#include "config_critical_alert_parser.inc"
#include "config_page_buttons_parser.inc"
#include "config_signals_loader.inc"
#include "config_validators.inc"
#include "json_helpers.inc"
#include "config_dashboard_stage.inc"
#include "config_signals_file.inc"
