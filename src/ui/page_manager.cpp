#include "page_manager.h"
#include "page_manager_internal.h"

#include "alert_banner.h"
#include "alert_takeover.h"
#include "bus_silent_line.h"
#include "control_splash.h"
#include "cut_band.h"
#include "day_night_auto.h"
#include "diag_drawer.h"
#include "ota_overlay.h"
#include "rev_limit_flash.h"
#include "error_bar.h"
#include "gesture_controller.h"
#include "config_rejected_screen.h"
#include "no_config_screen.h"
#include "theme_manager.h"
#include "top_bar.h"
#include "widget_factory.h"

#include "config/config_loader.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_stats.h"
#include "runtime/signal_store.h"

#include "diag/logger.h"
#include "diag/lvgl_assert_lock.h"
#include "diag/perf_counters.h"

#include <lvgl.h>
#include <string.h>

namespace PageManagerInternal {

Page s_pages[MAX_PAGES];
uint8_t s_pageCount = 0;
uint8_t s_currentIdx = 0;
bool s_rebuildRequested = false;

uint8_t s_pendingFreeIdx = 0xFF;
uint8_t s_pendingLazyBuildIdx = 0xFF;

void showConfigFailure() {
    const CfgRejection &rejection = ConfigLoader::getRejection();
    if (!rejection.present) {
        LOG_WARN("UI", "No dashboard config — showing the no-config screen");
        NoConfigScreen::show();
        return;
    }
    ConfigRejectedScreen::show(rejection);
}

} // namespace PageManagerInternal

void PageManager::init() {
    using namespace PageManagerInternal;

    LVGL_ASSERT_LOCKED();
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();

    GestureController::setSwipeHandler(onSwipe);

    s_pageCount = 0;
    s_currentIdx = 0;

    lv_obj_clean(lv_scr_act());

    ErrorBar::init();

    DiagDrawer::init();

    if (!dash.loaded) {
        showConfigFailure();
        return;
    }

    ThemeManager::init();
    DayNightAuto::init();
    TopBar::init();
    BusSilentLine::init();

    buildPageList();

    CutBand::init();
    AlertBanner::init();
    ControlSplash::init();
    AlertTakeover::init();

    LOG_INFO("UI", "PageManager initialized: %d pages", s_pageCount);
}

bool PageManager::navigateTo(const char *pageId) {
    using namespace PageManagerInternal;

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
    LVGL_ASSERT_LOCKED();
    if (s_pageCount == 0)
        return;
    showPage((s_currentIdx + 1) % s_pageCount);
}

void PageManager::navigatePrev() {
    using namespace PageManagerInternal;
    LVGL_ASSERT_LOCKED();
    if (s_pageCount == 0)
        return;
    showPage((s_currentIdx == 0) ? s_pageCount - 1 : s_currentIdx - 1);
}

int16_t PageManager::currentContentTopY() {
    using namespace PageManagerInternal;
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (s_pageCount == 0 || !dash.loaded)
        return TopBar::getHeight();
    return contentTopY(dash.pages[s_pages[s_currentIdx].cfgIdx]);
}

const char *PageManager::getDefaultPageId() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    return dash.defaultPageId;
}

void PageManager::requestRebuild() {
    PageManagerInternal::s_rebuildRequested = true;
}

void PageManager::updateWidgets() {
    using namespace PageManagerInternal;

    LVGL_ASSERT_LOCKED();
    if (OtaOverlay::isActive())
        return;

    if (s_pageCount == 0)
        return;

    if (s_rebuildRequested) {
        reapplyThemeAllPages();
        return;
    }

    GestureController::checkGestures();

    lv_obj_t *currentScreen = s_pages[s_currentIdx].screen;
    SignalStore::SignalValue snap[SIGNAL_STORE_MAX_SIGNALS];
    SignalStore::snapshotAll(snap);

    SignalStats::tick(snap);
    AlertEngine::tick(snap);
    RevLimitFlash::set(AlertEngine::getState().revLimiter == AlertEngine::AlertLevel::CRITICAL,
                       AlertEngine::isRevLimiterRowLit());
    CutBand::update(snap);
    {
        PERF_SCOPE(::PerfCounters::WIDGETS);
        WidgetFactory::updateAll(currentScreen, snap);
    }

    static uint32_t lastTopBarMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastTopBarMs > 200) {
        PERF_SCOPE(::PerfCounters::TOPBAR);
        TopBar::update();
        lastTopBarMs = nowMs;
    }

    static uint32_t lastTimeoutCheck = 0;
    uint32_t now = millis();
    if (now - lastTimeoutCheck > 100) {
        SignalStore::checkTimeouts();
        lastTimeoutCheck = now;
    }

    AlertBanner::update();
    AlertTakeover::update();
    BusSilentLine::update();
    ControlSplash::update();

    ErrorBar::update();
    DiagDrawer::update();
}
