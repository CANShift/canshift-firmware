// page_manager.cpp — Dashboard page lifecycle orchestrator + public API
//
// This translation unit owns the page manager state machine: file-static
// state definitions, the request* flag latches, the public PageManager::*
// surface, and the rebuild/reload pump driven from updateWidgets().
//
// The LVGL widget tree construction (buildPage / cruise template / theme
// rebuild) lives in page_manager_builder.cpp; the page transition + lazy
// build pump lives in page_manager_anim.cpp. Both reach into the state
// defined here via page_manager_internal.h.
//
// Critical preserved behaviour:
//   - #1295 dummy-screen UAF guard in rebuildAllPages (builder TU)
//   - #973 IconAssets::preloadDashboardAssets() bracketing around rebuild
//   - #1257 in-place theme reapply fast path
//   - #704 swipe→navigate adapter wiring via GestureController

#include "page_manager.h"
#include "page_manager_internal.h"

#include "burn_overlay.h"
#include "diag_drawer.h"
#include "error_bar.h"
#include "gesture_controller.h"
#include "screen_profile.h"
#include "setup_screen.h"
#include "theme_manager.h"
#include "top_bar.h"
#include "widget_factory.h"

#include "config/config_loader.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"

#include "diag/logger.h"
#include "diag/lvgl_assert_lock.h"
#include "diag/perf_counters.h"

#include <lvgl.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal state — defined here, declared `extern` in page_manager_internal.h
// so the builder + anim modules can read/write through PageManagerInternal::.
// ---------------------------------------------------------------------------

namespace PageManagerInternal {

Page s_pages[MAX_PAGES];
uint8_t s_pageCount = 0;
uint8_t s_currentIdx = 0;
lv_obj_t *s_revOverlay = nullptr; // Red flash overlay, global
bool s_rebuildRequested = false;  // Set by ThemeManager::toggleDayMode()
bool s_reloadRequested = false;   // Set by USB CMD_PUT_CONFIG handler

uint8_t s_pendingFreeIdx = 0xFF;
uint8_t s_pendingLazyBuildIdx = 0xFF;
uint32_t s_pendingLazyBuildMs = 120;

} // namespace PageManagerInternal

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PageManager::init() {
    using namespace PageManagerInternal;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();

    // Route page-nav swipes detected by the gesture controller back here so
    // showPage stays a TU-local function (#704).
    GestureController::setSwipeHandler(onSwipe);

    s_pageCount = 0;
    s_currentIdx = 0;

    // Free all splash-screen children from the LVGL pool before registering
    // pages. The boot sequence built its UI on lv_scr_act(); cleaning it here
    // recovers that pool space for widget allocations below.
    lv_obj_clean(lv_scr_act());

    // Init error bar first so errors pushed during boot (config load, CAN init)
    // are visible regardless of whether a valid dashboard config exists.
    ErrorBar::init();
    // Diag drawer shares lv_layer_top with the error bar (#635). Init after
    // it so the drawer's handle z-order sits cleanly above the bar.
    DiagDrawer::init();

    if (!dash.loaded) {
        LOG_WARN("UI", "No dashboard config — showing setup screen");
        SetupScreen::show();
        return;
    }

    // Load persisted day/night preference before building pages
    ThemeManager::init();

    // Initialize the top bar (persistent overlay, not part of any page)
    TopBar::init();

    // Register all visible pages and build only the default one eagerly.
    // All other pages are built lazily the first time they are navigated to
    // (see showPage). This keeps at most one extra page in the LVGL pool at
    // boot, avoiding OOM on configs with many gauge-heavy pages.
    const char *defaultId = dash.defaultPageId;
    for (uint8_t i = 0; i < dash.pageCount && s_pageCount < MAX_PAGES; ++i) {
        if (!dash.pages[i].visible) {
            LOG_INFO("UI", "Skipping hidden page '%s' (visible=false)", dash.pages[i].id);
            continue;
        }
        Page &p = s_pages[s_pageCount];
        strlcpy(p.id, dash.pages[i].id, CFG_MAX_ID_LEN);
        p.screen = nullptr;
        p.built = false;
        p.cfgIdx = i;

        // Build the default page eagerly so the first navigation is instant.
        if (strcmp(p.id, defaultId) == 0) {
            buildPage(s_pageCount, dash.pages[i]);
        }
        s_pageCount++;
    }

    // If the default page was not found among visible pages, build the first
    // visible one so the device always boots into a usable screen.
    if (s_pageCount > 0 && !s_pages[0].screen) {
        for (uint8_t i = 0; i < s_pageCount; ++i) {
            if (!s_pages[i].screen) {
                buildPage(i, dash.pages[s_pages[i].cfgIdx]);
                break;
            }
        }
    }

    // Create rev limiter overlay — sits above all pages
    // Hidden by default, shown by AlertEngine
    s_revOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_revOverlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_revOverlay, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_revOverlay, LV_OPA_40, LV_PART_MAIN); // 40% opacity
    lv_obj_add_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_revOverlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_revOverlay, LV_OBJ_FLAG_CLICKABLE);

    LOG_INFO("UI", "PageManager initialized: %d pages", s_pageCount);
}

bool PageManager::navigateTo(const char *pageId) {
    using namespace PageManagerInternal;

    // Canary for #158 LVGL mutex audit (2026-05-31). Page transitions touch
    // every widget tree under the new page — touching them from outside the
    // UI task hold window would race with the next lv_task_handler tick.
    // Debug builds crash here; release builds compile this to a no-op.
    LVGL_ASSERT_LOCKED();
    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (strcmp(s_pages[i].id, pageId) == 0) {
            showPage(i);
            return true;
        }
    }
    LOG_WARN("UI", "Page not found: %s", pageId);
    return false;
}

void PageManager::navigateNext() {
    using namespace PageManagerInternal;
    if (s_pageCount == 0)
        return;
    showPage((s_currentIdx + 1) % s_pageCount, LV_SCR_LOAD_ANIM_OVER_LEFT, SWIPE_ANIM_MS);
}

void PageManager::navigatePrev() {
    using namespace PageManagerInternal;
    if (s_pageCount == 0)
        return;
    showPage((s_currentIdx == 0) ? s_pageCount - 1 : s_currentIdx - 1, LV_SCR_LOAD_ANIM_OVER_RIGHT,
             SWIPE_ANIM_MS);
}

const char *PageManager::getDefaultPageId() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    return dash.defaultPageId;
}

void PageManager::requestRebuild() {
    PageManagerInternal::s_rebuildRequested = true;
}

void PageManager::requestReload() {
    PageManagerInternal::s_reloadRequested = true;
}

void PageManager::updateWidgets() {
    using namespace PageManagerInternal;

    // Reload supersedes a pending rebuild — the reload re-applies the theme
    // and rebuilds every page. Check it BEFORE the rebuild flag and clear
    // both so we don't double-rebuild on the next tick.
    if (s_reloadRequested) {
        s_reloadRequested = false;
        s_rebuildRequested = false;
        if (ConfigLoader::reloadAll()) {
            // Refresh design→physical scale factors before widgets reread
            // their layout — a hot reload may have changed targetProfile
            // (issues #17, #18).
            ScreenProfile::initFromDashboard();
            rebuildAllPages();
            BurnOverlay::hide();
        } else {
            LOG_ERROR("UI", "Config reload failed — keeping previous pages");
            BurnOverlay::showError(BurnOverlay::ErrorReason::ReloadFailed);
        }
        return; // Skip widget updates this tick; next tick runs normally
    }

    if (s_pageCount == 0)
        return;

    // Theme toggle takes the in-place reapply path (#1257). Structural
    // reloads still go through `rebuildAllPages` via `s_reloadRequested`.
    if (s_rebuildRequested) {
        reapplyThemeAllPages();
        return; // Skip widget updates this tick; next tick runs normally
    }

    // Process swipe gestures before widget updates so navigation changes take
    // effect on the same frame that LVGL renders.
    GestureController::checkGestures();

    // Update widgets on the current page
    lv_obj_t *currentScreen = s_pages[s_currentIdx].screen;
    {
        PERF_SCOPE(::PerfCounters::WIDGETS);
        WidgetFactory::updateAll(currentScreen);
    }

    // Refresh top bar status (ECU/CAN dots, voltage, page name, USB icon).
    // Throttled to ~5 Hz to keep frame budget reasonable.
    static uint32_t lastTopBarMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastTopBarMs > 200) {
        PERF_SCOPE(::PerfCounters::TOPBAR);
        TopBar::update();
        lastTopBarMs = nowMs;
    }

    // Check signal timeouts periodically (100ms interval is sufficient)
    static uint32_t lastTimeoutCheck = 0;
    uint32_t now = millis();
    if (now - lastTimeoutCheck > 100) {
        SignalStore::checkTimeouts();
        lastTimeoutCheck = now;
    }

    // Apply alert overlays
    AlertEngine::tick();
    setRevLimiterOverlay(AlertEngine::isRevLimiterFlashOn());

    ErrorBar::update();
    DiagDrawer::update();
}

void PageManager::setRevLimiterOverlay(bool visible) {
    using namespace PageManagerInternal;
    if (!s_revOverlay)
        return;
    if (visible) {
        lv_obj_clear_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}
