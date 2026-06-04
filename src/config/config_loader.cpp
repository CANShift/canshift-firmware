// config_loader.cpp — Orchestrator for the ConfigLoader public API (#1207).
//
// Owns the file-static config storage (`s_dashboard`, `s_signals`, `s_device`,
// `s_inputs`), the rollback snapshot buffer (PSRAM/BSS selection), and the
// thin public entry points that drive the four file loaders defined in
// `config_parser.cpp`. JSON primitives live in `json_helpers.cpp`; string-to-
// enum decoders + the schema-version check live in `config_validators.cpp`;
// the optional Rust FFI sits behind `config_loader_rust_bridge.h`.
//
// Splitting this file is purely a maintainability refactor — the public API
// in `config_loader.h` is byte-identical and the call graph from
// `ConfigLoader::loadAll()` down through the per-file loaders is unchanged.

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

// ---------------------------------------------------------------------------
// File-static config storage (declared `extern` in config_loader_internal.h
// so the helper TUs can mutate / read it without re-routing every parser
// through a getter).
// ---------------------------------------------------------------------------

namespace ConfigLoaderInternal {

CfgDashboard s_dashboard = {};
CfgSignalConfig s_signals = {};
CfgDeviceConfig s_device = {};
CfgInputBindings s_inputs = {};

// Transactional reload snapshot (issue #458, audit F-HI-3 / umbrella #1014).
// Previously these snapshots were heap-allocated on each load* entry and freed
// on exit (~22 KB + ~5 KB of malloc/free churn per reload), which fragmented
// the LVGL pool when fonts/SPIFFS loaded right after boot (related to #895 /
// #976). They were then moved to a single BSS-resident buffer so load*() never
// touches the heap.
//
// loadDashboard() and loadSignals() run sequentially on the same task
// (ConfigLoader::loadAll drives them in order) and each completes before the
// next begins, so a single shared buffer sized to the larger of the two types
// is enough. The static_assert pins the invariant so a future struct growth
// on the other type can't silently underflow this buffer.
//
// Storage selection (issue #1073):
//   - BOARD_HAS_PSRAM builds (crowpanel_28 / crowpanel_28_wifi) prefer a
//     one-shot PSRAM allocation. On a WROVER module this reclaims ~5 KB of
//     `dram0_0_seg` — exactly the room `crowpanel_28_wifi` needs to fit the
//     WiFi / mDNS / lwip BSS that the dash-hosted Studio WS bridge brings in
//     (issues #1073, #1108; the env link-overflowed by ~1.7 KB before this
//     change). On a WROOM module the runtime PSRAM probe in
//     `hal/memory/psram.cpp` reports 0 bytes, the alloc returns null, and
//     the rollback feature degrades to a no-op (parse failures no longer
//     restore the prior in-memory state — same risk profile as the pre-#458
//     single-buffer path, documented in the #1073 PR body).
//   - Non-PSRAM builds (host / native test, `[env:native]`) keep the BSS
//     buffer so unit tests still exercise the rollback path byte-for-byte.
static_assert(sizeof(CfgDashboard) >= sizeof(CfgSignalConfig),
              "rollback snapshot buffer must fit CfgDashboard (the larger of the two)");
namespace {
constexpr size_t kRollbackSnapshotSize = sizeof(CfgDashboard);

#ifndef BOARD_HAS_PSRAM
alignas(CfgDashboard) uint8_t s_rollback_snapshot_bss[kRollbackSnapshotSize];
#endif
} // namespace

// Returns the snapshot buffer or nullptr when no backing storage is available.
//   - BOARD_HAS_PSRAM: lazy-allocate from PSRAM on first call; nullptr on the
//     WROOM no-PSRAM runtime path so callers skip the snapshot step.
//   - Otherwise: returns the BSS reservation (cannot fail).
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
