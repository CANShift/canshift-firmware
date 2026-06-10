#include "page_manager_internal.h"

#include "app_config.h"
#include "widget_factory.h"

#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"

#include <lvgl.h>

namespace PageManagerInternal {

namespace {

// Deferred — lv_obj_del inside an LVGL event handler frees the handler's tag
// (use-after-free). lv_async_call runs us at the next timer iteration.
void asyncDoLazyBuild(void * /*unused*/) {
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
        }
        WidgetFactory::clearAll(dep.screen);
        lv_obj_del(dep.screen);
        dep.screen = nullptr;
        dep.built = false;
        s_pendingFreeIdx = 0xFF;
        LOG_INFO("UI", "Released page '%s' before lazy build of '%s'", dep.id, s_pages[idx].id);
    }

    LOG_INFO("UI", "Lazy-building page '%s' on first visit", s_pages[idx].id);
    buildPage(idx, dash.pages[s_pages[idx].cfgIdx]);
    if (!s_pages[idx].screen) {
        LOG_ERROR("UI", "asyncDoLazyBuild: build failed for page '%s'", s_pages[idx].id);
        return;
    }

    PERF_RECORD_PAGE_XSTART();
    lv_scr_load_anim(s_pages[idx].screen, LV_SCR_LOAD_ANIM_FADE_IN, s_pendingLazyBuildMs, 0,
                     placeholderActive);
    s_currentIdx = idx;
    LOG_INFO("UI", "Navigated to page '%s' (idx=%u)", s_pages[idx].id, idx);
}

} // namespace

// OVER_LEFT/RIGHT for swipes (only the moving screen repaints — #64);
// FADE_IN for programmatic nav.
void showPage(uint8_t idx, lv_scr_load_anim_t anim, uint32_t durationMs) {
    if (idx >= s_pageCount) {
        LOG_WARN("UI", "showPage: idx=%u out of range (pageCount=%u)", idx, s_pageCount);
        return;
    }
    LOG_INFO("UI", "showPage: %u -> %u anim=%d", s_currentIdx, idx, static_cast<int>(anim));

    // Release before building so the freed pool is available.
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

    // Lazy build via lv_async_call — see asyncDoLazyBuild comment. Coalesce
    // pending requests latest-wins so a stale enqueue doesn't fire.
    if (!s_pages[idx].screen) {
        if (s_pendingLazyBuildIdx != 0xFF) {
            LOG_DEBUG("UI", "Lazy build already pending for idx=%u; coalescing new request idx=%u",
                      s_pendingLazyBuildIdx, idx);
            s_pendingLazyBuildIdx = idx;
            s_pendingLazyBuildMs = durationMs;
            return;
        }
        s_pendingLazyBuildIdx = idx;
        s_pendingLazyBuildMs = durationMs;
        lv_async_call(asyncDoLazyBuild, nullptr);
        return;
    }

    PERF_RECORD_PAGE_XSTART();
    lv_scr_load_anim(s_pages[idx].screen, anim, durationMs, 0, false);

#if APP_PROFILE_UI
    lv_timer_t *t = lv_timer_create(
        [](lv_timer_t *self) {
            PERF_RECORD_PAGE_XEND();
            lv_timer_del(self);
        },
        durationMs, nullptr);
    if (t)
        lv_timer_set_repeat_count(t, 1);
#endif

    // Defer release until next showPage — still needed for the current anim.
    if (s_currentIdx != idx) {
        s_pendingFreeIdx = s_currentIdx;
    }

    s_currentIdx = idx;
    LOG_INFO("UI", "Navigated to page '%s' (idx=%u)", s_pages[idx].id, idx);
}

void showPage(uint8_t idx) {
    showPage(idx, LV_SCR_LOAD_ANIM_FADE_IN, 120);
}

void onSwipe(lv_dir_t dir) {
    if (s_pageCount <= 1)
        return;
    // Drift on a clickable widget reads as tap intent, not navigation.
    lv_obj_t *pressed = lv_indev_get_obj_act();
    if (pressed && lv_obj_has_flag(pressed, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    switch (dir) {
        case LV_DIR_LEFT:
            showPage((s_currentIdx + 1) % s_pageCount, LV_SCR_LOAD_ANIM_OVER_LEFT, SWIPE_ANIM_MS);
            LOG_VDEBUG("UI", "Gesture: swipe left → next page");
            break;
        case LV_DIR_RIGHT:
            showPage(s_currentIdx == 0 ? s_pageCount - 1 : s_currentIdx - 1,
                     LV_SCR_LOAD_ANIM_OVER_RIGHT, SWIPE_ANIM_MS);
            LOG_VDEBUG("UI", "Gesture: swipe right → prev page");
            break;
        default:
            break;
    }
}

} // namespace PageManagerInternal
