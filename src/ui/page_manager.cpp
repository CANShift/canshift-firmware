#include "page_manager.h"
#include "page_manager_internal.h"

#include "diag_drawer.h"
#include "ota_overlay.h"
#include "error_bar.h"
#include "gesture_controller.h"
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

namespace PageManagerInternal {

Page s_pages[MAX_PAGES];
uint8_t s_pageCount = 0;
uint8_t s_currentIdx = 0;
lv_obj_t *s_revOverlay = nullptr;
bool s_rebuildRequested = false;

uint8_t s_pendingFreeIdx = 0xFF;
uint8_t s_pendingLazyBuildIdx = 0xFF;
uint32_t s_pendingLazyBuildMs = 120;

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
        LOG_WARN("UI", "No dashboard config — showing setup screen");
        SetupScreen::show();
        return;
    }

    ThemeManager::init();
    TopBar::init();

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

        if (strcmp(p.id, defaultId) == 0) {
            buildPage(s_pageCount, dash.pages[i]);
        }
        s_pageCount++;
    }

    if (s_pageCount > 0 && !s_pages[0].screen) {
        for (uint8_t i = 0; i < s_pageCount; ++i) {
            if (!s_pages[i].screen) {
                buildPage(i, dash.pages[s_pages[i].cfgIdx]);
                break;
            }
        }
    }

    s_revOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_revOverlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_revOverlay, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_revOverlay, LV_OPA_40, LV_PART_MAIN);
    lv_obj_add_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_revOverlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_revOverlay, LV_OBJ_FLAG_CLICKABLE);

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
    showPage((s_currentIdx + 1) % s_pageCount, LV_SCR_LOAD_ANIM_OVER_LEFT, SWIPE_ANIM_MS);
}

void PageManager::navigatePrev() {
    using namespace PageManagerInternal;
    LVGL_ASSERT_LOCKED();
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
    {
        PERF_SCOPE(::PerfCounters::WIDGETS);
        WidgetFactory::updateAll(currentScreen);
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

    AlertEngine::tick();
    setRevLimiterOverlay(AlertEngine::isRevLimiterFlashOn());

    ErrorBar::update();
    DiagDrawer::update();
}

void PageManager::setRevLimiterOverlay(bool visible) {
    using namespace PageManagerInternal;
    LVGL_ASSERT_LOCKED();
    if (!s_revOverlay)
        return;
    if (visible) {
        lv_obj_clear_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}
