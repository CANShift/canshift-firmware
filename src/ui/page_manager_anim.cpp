#include "page_manager_internal.h"

#include "app_config.h"
#include "widget_factory.h"

#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"

#include <lvgl.h>

namespace PageManagerInternal {

namespace {

void asyncDoLazyBuild(void *) {
    const uint8_t idx = s_pendingLazyBuildIdx;
    s_pendingLazyBuildIdx = 0xFF;

    if (idx >= s_pageCount || s_pages[idx].screen)
        return;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    bool placeholderActive = false;

    if (s_currentIdx != idx && s_pages[s_currentIdx].screen) {
        Page &dep = s_pages[s_currentIdx];
        lv_obj_t *blank = lv_obj_create(nullptr);
        if (blank) {
            lv_obj_set_style_bg_color(blank, lv_color_hex(0x000000), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, LV_PART_MAIN);
            lv_scr_load(blank);
            placeholderActive = true;
            WidgetFactory::clearAll(dep.screen);
            lv_obj_del(dep.screen);
            dep.screen = nullptr;
            dep.built = false;
            s_pendingFreeIdx = 0xFF;
            LOG_INFO("UI", "Released page '%s' before lazy build of '%s'", dep.id, s_pages[idx].id);
        } else {
            // Deleting dep.screen while it is still the active screen would
            // leave LVGL's active-screen pointer dangling.
            LOG_WARN("UI", "asyncDoLazyBuild: placeholder alloc failed — keeping page '%s'",
                     dep.id);
        }
    }

    LOG_INFO("UI", "Lazy-building page '%s' on first visit", s_pages[idx].id);
    buildPage(idx, dash.pages[s_pages[idx].cfgIdx]);
    if (!s_pages[idx].screen) {
        LOG_ERROR("UI", "asyncDoLazyBuild: build failed for page '%s'", s_pages[idx].id);
        return;
    }

    PERF_RECORD_PAGE_XSTART();
    lv_scr_load_anim(s_pages[idx].screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, placeholderActive);
    s_currentIdx = idx;
    LOG_INFO("UI", "Navigated to page '%s' (idx=%u)", s_pages[idx].id, idx);
}

} // namespace

void showPage(uint8_t idx) {
    if (idx >= s_pageCount) {
        LOG_WARN("UI", "showPage: idx=%u out of range (pageCount=%u)", idx, s_pageCount);
        return;
    }
    LOG_INFO("UI", "showPage: %u -> %u", s_currentIdx, idx);

    if (s_pendingFreeIdx < s_pageCount && s_pendingFreeIdx != idx) {
        Page &old = s_pages[s_pendingFreeIdx];
        if (old.screen) {
            WidgetFactory::clearAll(old.screen);
            lv_obj_del(old.screen);
            old.screen = nullptr;
            old.built = false;
            LOG_INFO("UI", "Released page '%s' from LVGL pool after navigation", old.id);
        }
        s_pendingFreeIdx = 0xFF;
    }

    if (!s_pages[idx].screen) {
        if (s_pendingLazyBuildIdx != 0xFF) {
            LOG_DEBUG("UI", "Lazy build already pending for idx=%u; coalescing new request idx=%u",
                      s_pendingLazyBuildIdx, idx);
            s_pendingLazyBuildIdx = idx;
            return;
        }
        s_pendingLazyBuildIdx = idx;
        lv_async_call(asyncDoLazyBuild, nullptr);
        return;
    }

    PERF_RECORD_PAGE_XSTART();
    lv_scr_load_anim(s_pages[idx].screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

#if APP_PROFILE_UI
    PERF_RECORD_PAGE_XEND();
#endif

    if (s_currentIdx != idx) {
        s_pendingFreeIdx = s_currentIdx;
    }

    s_currentIdx = idx;
    LOG_INFO("UI", "Navigated to page '%s' (idx=%u)", s_pages[idx].id, idx);
}

void onSwipe(lv_dir_t dir) {
    if (s_pageCount <= 1)
        return;

    lv_obj_t *pressed = lv_indev_get_obj_act();
    if (pressed && lv_obj_has_flag(pressed, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    switch (dir) {
        case LV_DIR_LEFT:
            showPage((s_currentIdx + 1) % s_pageCount);
            LOG_VDEBUG("UI", "Gesture: swipe left → next page");
            break;
        case LV_DIR_RIGHT:
            showPage(s_currentIdx == 0 ? s_pageCount - 1 : s_currentIdx - 1);
            LOG_VDEBUG("UI", "Gesture: swipe right → prev page");
            break;
        default:
            break;
    }
}

} // namespace PageManagerInternal
