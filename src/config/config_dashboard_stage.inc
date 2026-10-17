#include "config_loader_internal.h"

#include "board_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "json_reader.h"

#include <ArduinoJson.h>
#include <cstdio>
#include <memory>
#include <new>

namespace ConfigLoaderInternal {

namespace {

using ConfigLoader::DashboardOutcome;
using StagedDashboard = std::unique_ptr<CfgDashboard>;

DashboardOutcome s_dashboardOutcome = DashboardOutcome::Unreadable;

DashboardOutcome reportUnreadable() {
    LOG_ERROR("CFG", "dashboard.json unreadable (primary + .bak)");
    char msg[48];
    snprintf(msg, sizeof(msg), "dashboard.json: %s", lastParseError());
    ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", msg);
    return DashboardOutcome::Unreadable;
}

DashboardOutcome stageDashboard(StagedDashboard &out) {
    s_rejection = {};
    out.reset(new (std::nothrow) CfgDashboard{});
    if (!out) {
        LOG_ERROR("CFG", "loadDashboard: staging alloc (%u B) failed",
                  static_cast<unsigned>(sizeof(CfgDashboard)));
        ErrorStore::push(ERROR_SRC_CONFIG, "OOM", "dashboard staging alloc failed");
        return DashboardOutcome::NoMemory;
    }
    JsonDocument doc(JsonReader::configAllocator());
    const DashboardOutcome outcome = readAndParseWithBak(kConfigPathDashboard, doc)
                                         ? buildDashboard(doc, out.get())
                                         : reportUnreadable();
    if (outcome != DashboardOutcome::Loaded)
        out.reset();
    return outcome;
}

bool restorePreviousDashboard() {
    if (!promoteBackup(kConfigPathDashboard)) {
        LOG_WARN("CFG", "dashboard.json refused — no previous file to restore");
        return false;
    }
    LOG_WARN("CFG", "dashboard.json refused — previous file restored from .bak");
    ErrorStore::push(ERROR_SRC_CONFIG, "CFG_BAK", "dashboard.json restored from .bak");
    return true;
}

DashboardOutcome stageWithFallback(StagedDashboard &out) {
    const DashboardOutcome first = stageDashboard(out);
    if (first != DashboardOutcome::Refused || !restorePreviousDashboard())
        return first;
    return stageDashboard(out) == DashboardOutcome::Loaded ? DashboardOutcome::RestoredPrevious
                                                           : DashboardOutcome::Refused;
}

} // namespace

bool loadDashboard() {
    StagedDashboard staged;
    s_dashboardOutcome = stageWithFallback(staged);
    if (!staged)
        return false;
    staged->loaded = true;
    LOG_INFO("CFG", "dashboard.json loaded: %d pages", staged->pageCount);
    CfgDashboard *prev = s_dashboard;
    s_dashboard = staged.release();
    delete prev;
    return true;
}

} // namespace ConfigLoaderInternal

ConfigLoader::DashboardOutcome ConfigLoader::lastDashboardOutcome() {
    return ConfigLoaderInternal::s_dashboardOutcome;
}

ConfigLoader::DashboardOutcome ConfigLoader::vetDashboard() {
    ConfigLoaderInternal::StagedDashboard staged;
    ConfigLoaderInternal::s_dashboardOutcome = ConfigLoaderInternal::stageWithFallback(staged);
    return ConfigLoaderInternal::s_dashboardOutcome;
}
