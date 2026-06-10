#include "config_loader.h"

#include "app_config.h"
#include "config_loader_internal.h"
#include "diag/logger.h"

#include <cstdint>
#include <cstring>

#ifdef ARDUINO
    #include "hal/memory/psram.h"
    #include <esp_heap_caps.h>
#endif

namespace ConfigLoaderInternal {

CfgDashboard s_dashboard = {};
CfgSignalConfig s_signals = {};
CfgDeviceConfig s_device = {};
CfgInputBindings s_inputs = {};

// Single shared rollback snapshot buffer — sized for the larger struct (#458,
// F-HI-3). loadDashboard/loadSignals run sequentially so a shared buffer is
// enough. PSRAM-backed on WROVER to reclaim ~5 KB DRAM (#1073).
static_assert(sizeof(CfgDashboard) >= sizeof(CfgSignalConfig),
              "rollback snapshot buffer must fit CfgDashboard");
namespace {
constexpr size_t kRollbackSnapshotSize = sizeof(CfgDashboard);

#ifndef BOARD_HAS_PSRAM
alignas(CfgDashboard) uint8_t s_rollback_snapshot_bss[kRollbackSnapshotSize];
#endif
} // namespace

// nullptr when the WROOM no-PSRAM path can't back the snapshot — callers skip.
uint8_t *acquireRollbackSnapshot() {
#ifdef BOARD_HAS_PSRAM
    static uint8_t *s_rollback_snapshot_psram = nullptr;
    static bool s_psram_alloc_attempted = false;
    if (!s_psram_alloc_attempted) {
        s_psram_alloc_attempted = true;
    #ifdef ARDUINO
        if (canshift::hal::memory::isPsramAvailable()) {
            s_rollback_snapshot_psram =
                static_cast<uint8_t *>(heap_caps_malloc(kRollbackSnapshotSize, MALLOC_CAP_SPIRAM));
            if (s_rollback_snapshot_psram) {
                LOG_INFO("CFG", "rollback snapshot (%u B) allocated in PSRAM",
                         static_cast<unsigned>(kRollbackSnapshotSize));
            } else {
                LOG_WARN("CFG", "rollback snapshot PSRAM alloc (%u B) failed — rollback disabled",
                         static_cast<unsigned>(kRollbackSnapshotSize));
            }
        } else {
            LOG_WARN("CFG", "no PSRAM detected — rollback snapshot disabled (WROOM variant; "
                            "parse-failure restore will no-op)");
        }
    #endif
    }
    return s_rollback_snapshot_psram;
#else
    return s_rollback_snapshot_bss;
#endif
}

} // namespace ConfigLoaderInternal

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ConfigLoader::LoadResult ConfigLoader::loadAll() {
    LoadResult r{}; // value-init — every member is assigned below
    r.dashboardOk = ConfigLoaderInternal::loadDashboard();
    r.signalsOk = ConfigLoaderInternal::loadSignals();
    r.deviceOk = ConfigLoaderInternal::loadDevice();
    // loadInputBindings depends on the parsed device config for pin-conflict
    // detection, so it must run AFTER loadDevice.
    r.inputsOk = ConfigLoaderInternal::loadInputBindings();
    return r;
}

const CfgDashboard &ConfigLoader::getDashboardConfig() {
    return ConfigLoaderInternal::s_dashboard;
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
bool ConfigLoader::reloadAll() {
    LoadResult r = loadAll();
    LOG_INFO("CFG", "Config reloaded: dashboard=%d signals=%d inputs=%d", r.dashboardOk,
             r.signalsOk, r.inputsOk);
    return r.dashboardOk; // Dashboard is mandatory
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

// ---------------------------------------------------------------------------
// Unity build — the helper TUs are pulled into this TU so the production
// build (`-O2`, no `-flto`) can inline cross-helper calls. Splitting them as
// separate `.cpp` files preserved logical separation but pushed flash usage
// to 98.2% (#1207 split CI fail). Including them here keeps the file-level
// boundaries intact while restoring the single-TU optimization surface.
// Native tests compile this single TU too — `build_src_filter` no longer
// lists the helpers individually since they are pulled in here.
// ---------------------------------------------------------------------------
#include "config_file_loaders.inc"
#include "config_parser.inc"
#include "config_validators.inc"
#include "json_helpers.inc"
