// page_manager_anim.cpp — Page transitions + lazy build pump + swipe gesture
//
// Split out of page_manager.cpp during the #1207 refactor. Owns:
//   - showPage(): the main navigation entry point, including the deferred
//     lazy-build path (lv_async_call → asyncDoLazyBuild) that ensures
//     lv_obj_del never fires inside an LVGL event callback
//   - onSwipe(): the swipe→navigate adapter registered with GestureController
//
// State (s_pages, s_currentIdx, s_pendingFreeIdx, s_pendingLazyBuildIdx,
// s_pendingLazyBuildMs) is owned by page_manager.cpp; this TU reaches in via
// page_manager_internal.h.

#include "page_manager_internal.h"

#include "app_config.h"
#include "widget_factory.h"

#include "config/config_loader.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"

#include <lvgl.h>

namespace PageManagerInternal {

namespace {

// Deferred lazy-build callback — invoked by lv_async_call() at the start of
// the next lv_timer_handler() iteration, outside any event callback. This
// avoids the use-after-free that occurs when lv_obj_del(dep.screen) is called
// synchronously from within a button click handler: lv_obj_del fires
// LV_EVENT_DELETE on the button, which frees its tag, and the click handler
// then continues with a dangling pointer.
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

// Page transitions on the ESP32:
//  - OVER_LEFT/RIGHT: new screen slides over old, old stays static. Only the
//    moving screen is repainted each frame — cheapest per-pixel option without
//    a GPU. Used for swipe gestures (issue #64, restoring PR #59's choice
//    after #331 swapped it for MOVE_* which redraws both screens every frame
//    and dominated the per-frame budget during transitions).
//  - FADE_IN: alpha-blended cross-fade. Costly per pixel on a 320×240 panel
//    without a GPU; we keep it short. Used for programmatic navigation where
//    no direction is implied.
void showPage(uint8_t idx, lv_scr_load_anim_t anim, uint32_t durationMs) {
    if (idx >= s_pageCount) {
        LOG_WARN("UI", "showPage: idx=%u out of range (pageCount=%u)", idx, s_pageCount);
        return;
    }
    LOG_INFO("UI", "showPage: %u -> %u anim=%d", s_currentIdx, idx, static_cast<int>(anim));

    // Release the page that finished its last animation. Do this BEFORE building
    // the new page so the freed pool space is available. Skip if the user
    // navigated back to the very page that was scheduled for release.
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

    // Lazy build: schedule via lv_async_call so lv_obj_del never fires from
    // within an event callback (button click → navigateTo → showPage).
    // asyncDoLazyBuild() runs at the start of the next lv_timer_handler()
    // iteration, safely outside the call stack of any event handler.
    //
    // Coalesce concurrent requests: if a lazy build is already scheduled but
    // the async callback has not yet drained, overwrite the pending idx with
    // the latest request (latest-wins debounce) and skip the second
    // lv_async_call. This prevents a stale enqueue and matches typical UI
    // debounce semantics — the user's most recent navigation intent wins.
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
    lv_scr_load_anim(s_pages[idx].screen, anim, durationMs, 0, false /* keep old screen */);

#if APP_PROFILE_UI
    // Schedule a one-shot LVGL timer at the animation end so the PERF
    // aggregator gets a wall-clock sample. lv_timer_create + repeat count 1
    // is the canonical pattern for one-shots in LVGL 8.3.
    lv_timer_t *t = lv_timer_create(
        [](lv_timer_t *self) {
            PERF_RECORD_PAGE_XEND();
            lv_timer_del(self);
        },
        durationMs, nullptr);
    if (t)
        lv_timer_set_repeat_count(t, 1);
#endif

    // Schedule the departing page for pool release on the next navigation.
    // It must stay alive for the current animation window so LVGL can render
    // the cross-fade/slide; we free it at the top of the next showPage() call.
    if (s_currentIdx != idx) {
        s_pendingFreeIdx = s_currentIdx;
    }

    s_currentIdx = idx;
    LOG_INFO("UI", "Navigated to page '%s' (idx=%u)", s_pages[idx].id, idx);
}

void showPage(uint8_t idx) {
    showPage(idx, LV_SCR_LOAD_ANIM_FADE_IN, 120);
}

// ---------------------------------------------------------------------------
// Gesture handling — extracted to gesture_controller (#704). PageManager
// receives swipe events via the callback registered in init().
// ---------------------------------------------------------------------------

void onSwipe(lv_dir_t dir) {
    if (s_pageCount <= 1)
        return;
    // If the touch that produced this swipe started on a clickable widget
    // (a button), treat the input as a tap attempt with finger drift — NOT
    // a navigation. Same finger drift that crosses LVGL's 40 px gesture
    // threshold otherwise yanks the user off the page mid-press. Swipes
    // that start in non-clickable background area (e.g. gaps between
    // widgets) still navigate as before.
    lv_obj_t *pressed = lv_indev_get_obj_act();
    if (pressed && lv_obj_has_flag(pressed, LV_OBJ_FLAG_CLICKABLE)) {
        return;
    }
    switch (dir) {
        case LV_DIR_LEFT:
            // Next page enters from the right, slides left — matches finger motion.
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
