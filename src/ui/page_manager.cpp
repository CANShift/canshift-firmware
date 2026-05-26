// page_manager.cpp — Dashboard page lifecycle and navigation

#include "page_manager.h"
#include "ui/burn_overlay.h"
#include "widget_factory.h"
#include "top_bar.h"
#include "diag_drawer.h"
#include "error_bar.h"
#include "gesture_controller.h"
#include "icon_assets.h"
#include "setup_screen.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "runtime/alert_engine.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"
#include "app_config.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

namespace {

static constexpr uint8_t MAX_PAGES = CONFIG_MAX_PAGES;

struct Page {
    char id[CFG_MAX_ID_LEN];
    lv_obj_t *screen; // LVGL screen object — nullptr until first navigation (lazy build)
    bool built;
    uint8_t cfgIdx; // index into CfgDashboard::pages[] for lazy build
};

static Page s_pages[MAX_PAGES];
static uint8_t s_pageCount = 0;
static uint8_t s_currentIdx = 0;
static lv_obj_t *s_revOverlay = nullptr; // Red flash overlay, global
static bool s_rebuildRequested = false;  // Set by ThemeManager::toggleDayMode()
static bool s_reloadRequested = false;   // Set by USB CMD_PUT_CONFIG handler
// Page scheduled for pool release after the in-flight animation completes.
// 0xFF = nothing pending. Consumed at the start of the next showPage() call so
// the departing screen stays alive for the full animation window (120 ms) before
// its LVGL objects are deleted.
static uint8_t s_pendingFreeIdx = 0xFF;
// Deferred lazy-build state — set by showPage(), consumed by asyncDoLazyBuild().
// Using lv_async_call defers the lv_obj_del to the next lv_timer_handler()
// iteration so it never fires from within an event callback.
static uint8_t s_pendingLazyBuildIdx = 0xFF;
static uint32_t s_pendingLazyBuildMs = 120;

// ---------------------------------------------------------------------------
// Cruise control template (issue #451) — procedural 2×2 button grid.
//
// Drawn instead of `cfg.widgets[]` whenever `page.templateKind ==
// CRUISE_CONTROL`. Each button is synthesised as a temporary CfgWidget and
// pushed through WidgetFactory::create so all of the existing button styling,
// theming, click-dispatch, and pool-lifecycle code keeps working unchanged —
// this template only chooses WHERE the buttons sit and WHICH cruise op they
// dispatch.
//
// Layout (firmware pixels, native 320×240 panel):
//   - 8 px outer padding so the buttons don't kiss the screen edge
//   - 12 px horizontal gap, 10 px vertical gap between cells
//   - Each cell is 140×85 fw-px → well above the 48×48 thumb-tap minimum
//     established by #117 for the day/night toggle on the top bar
//
// The studio mirrors this exact layout in CruiseControlPreview.tsx — keep
// the two in lock-step.
// ---------------------------------------------------------------------------

constexpr int16_t CRUISE_BUTTON_W = 140;
constexpr int16_t CRUISE_BUTTON_H = 85;
constexpr int16_t CRUISE_GAP_X = 12;
constexpr int16_t CRUISE_GAP_Y = 10;
constexpr int16_t CRUISE_OUTER_PAD = 8;

struct CruiseButtonSpec {
    const char *id;
    const char *label;
    CfgCruiseOp op;
};

constexpr CruiseButtonSpec CRUISE_BUTTONS[4] = {
    {"cruise_plus", "+", CfgCruiseOp::INCREMENT},
    {"cruise_set", "SET", CfgCruiseOp::SET},
    {"cruise_minus", "-", CfgCruiseOp::DECREMENT},
    {"cruise_off", "OFF", CfgCruiseOp::OFF},
};

// Build a synthetic CfgWidget representing one cruise button. Returned by
// value — small struct (~few hundred bytes), short-lived, only used to feed
// WidgetFactory::create() which copies the relevant fields it needs.
CfgWidget makeCruiseButton(const CruiseButtonSpec &spec, const CfgPage &pageCfg, int16_t x,
                           int16_t y) {
    CfgWidget w = {};
    strlcpy(w.id, spec.id, CFG_MAX_ID_LEN);
    w.type = WidgetType::BUTTON;
    w.signalId[0] = '\0';
    w.layout.x = x;
    w.layout.y = y;
    w.layout.w = CRUISE_BUTTON_W;
    w.layout.h = CRUISE_BUTTON_H;
    w.layout.zOrder = 0;

    // Reuse the page palette so the buttons follow day/night theming. The
    // background mirrors the page so the button face stays subtle and the
    // label reads as the primary visual.
    w.style.primaryColor = pageCfg.bgColor;
    w.style.textColor = CfgColor{0xFFFFFFu};
    w.style.fontSize = 22;

    CfgButtonParams &p = w.button;
    strlcpy(p.label, spec.label, CFG_MAX_NAME_LEN);
    p.iconPath[0] = '\0';
    p.iconName[0] = '\0';
    p.isToggle = false;
    p.showIcon = false;
    p.showLabel = true;
    p.hasColors = false;
    p.actionsCount = 1;

    CfgButtonAction &a = p.actions[0];
    a.type = CfgButtonActionType::CRUISE_CONTROL;
    a.pageId[0] = '\0';
    a.mapIndex = 0;
    a.canFrameId = 0;
    a.canDataLen = 0;
    a.canDataOffLen = 0;
    a.canExtended = false;
    a.cruiseOp = spec.op;
    a.cruiseStepKmh = 0; // 0 = use firmware default

    return w;
}

void buildCruiseControlTemplate(lv_obj_t *screen, const CfgPage &cfg, int16_t contentY) {
    // Centre the 2×2 grid in the available content area (below the top bar
    // when one is configured). Falls back to the outer-pad anchor if the
    // screen is narrower/shorter than the grid — defensive only; the native
    // 320×240 panel comfortably fits the layout.
    const int16_t gridW = CRUISE_BUTTON_W * 2 + CRUISE_GAP_X;
    const int16_t gridH = CRUISE_BUTTON_H * 2 + CRUISE_GAP_Y;
    const int16_t contentH = LV_VER_RES - contentY;
    int16_t startX = (LV_HOR_RES - gridW) / 2;
    int16_t startY = contentY + (contentH - gridH) / 2;
    if (startX < CRUISE_OUTER_PAD)
        startX = CRUISE_OUTER_PAD;
    if (startY < contentY + CRUISE_OUTER_PAD)
        startY = contentY + CRUISE_OUTER_PAD;

    uint8_t created = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t col = i % 2;
        const uint8_t row = i / 2;
        const int16_t x = startX + col * (CRUISE_BUTTON_W + CRUISE_GAP_X);
        // yOffset==0 here because we already baked the top-bar offset into the
        // synthetic widget's layout.y. WidgetFactory::create adds yOffset on
        // top of layout.y, so passing 0 keeps the buttons where we placed them.
        const int16_t y = startY + row * (CRUISE_BUTTON_H + CRUISE_GAP_Y);
        const CfgWidget w = makeCruiseButton(CRUISE_BUTTONS[i], cfg, x, y);
        if (WidgetFactory::create(screen, w, /*yOffset=*/0) != nullptr)
            ++created;
    }
    LOG_INFO("UI", "Built cruise_control template on page '%s' (%u/4 buttons)", cfg.id, created);
}

void applyPageBackground(lv_obj_t *screen, const CfgPage &cfg, const CfgColor &effectiveBg) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(effectiveBg.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    // Load background image from SPIFFS if set.
    // Requires LvglFsDriver registered (boot_sequence.cpp).
    // Path in cfg.bgImagePath is a SPIFFS path (e.g. "/images/bg.bmp").
    if (strlen(cfg.bgImagePath) > 0) {
        // Build LVGL FS path: "S:" + SPIFFS path
        static char lvglPath[CFG_MAX_PATH_LEN + 4];
        snprintf(lvglPath, sizeof(lvglPath), "S:%s", cfg.bgImagePath);

        lv_obj_t *img = lv_img_create(screen);
        lv_img_set_src(img, lvglPath);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_opa(img, LV_OPA_COVER, LV_PART_MAIN);
        LOG_DEBUG("UI", "Background image: %s", lvglPath);
    }
}

void buildPage(uint8_t idx, const CfgPage &cfg) {
    LOG_INFO("UI", "buildPage(%s) entry: heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    Page &p = s_pages[idx];
    strlcpy(p.id, cfg.id, CFG_MAX_ID_LEN);

    // Create an LVGL screen for each page
    p.screen = lv_obj_create(nullptr); // nullptr = new screen
    lv_obj_set_size(p.screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(p.screen, LV_OBJ_FLAG_SCROLLABLE);

    // Apply theme-aware background (day vs night). Pass the resolved color
    // by value to avoid copying the multi-kB CfgPage onto the stack.
    applyPageBackground(p.screen, cfg, ThemeManager::getEffectiveBgColor(cfg.bgColor));

    // Adjust content area for top bar
    int16_t contentY = cfg.showTopBar ? TopBar::getHeight() : 0;

    // Template-rendered pages (issue #451) bypass the free-form widgets[]
    // array entirely. The procedural builder synthesises CfgWidgets in place
    // and routes them through WidgetFactory::create so theming, click
    // dispatch, and pool lifecycle stay shared with the custom path.
    if (cfg.templateKind == CfgPageTemplate::CRUISE_CONTROL) {
        buildCruiseControlTemplate(p.screen, cfg, contentY);
        p.built = true;
        if (cfg.widgetCount > 0) {
            LOG_INFO("UI", "Page '%s': cruise_control template — ignoring %u user widget(s)",
                     cfg.id, static_cast<unsigned>(cfg.widgetCount));
        }
        LOG_INFO("UI", "buildPage(%s) exit:  heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(ESP.getFreeHeap()),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        return;
    }

    // Create all widgets for this page. Count successes so we surface a
    // visible warning if any silently failed — this is the diagnostic hook
    // for #57 ("some gauges not rendered").
    uint8_t created = 0;
    for (uint8_t w = 0; w < cfg.widgetCount; ++w) {
        const CfgWidget &wCfg = cfg.widgets[w];
        if (WidgetFactory::create(p.screen, wCfg, contentY) != nullptr)
            ++created;
    }

    p.built = true;
    if (created < cfg.widgetCount) {
        LOG_WARN("UI", "Page '%s': only %u/%u widgets built — see prior WF errors", cfg.id,
                 static_cast<unsigned>(created), static_cast<unsigned>(cfg.widgetCount));
    } else {
        LOG_INFO("UI", "Built page '%s' with %u widgets", cfg.id,
                 static_cast<unsigned>(cfg.widgetCount));
    }

    LOG_INFO("UI", "buildPage(%s) exit:  heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void rebuildAllPages() {
    s_rebuildRequested = false;
    s_pendingFreeIdx = 0xFF; // All pages are about to be deleted — nothing to defer.

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || s_pageCount == 0)
        return;

    // Pre-warm the image cache BEFORE the destructive teardown below. At this
    // point the heap is still in its "between-rebuilds" state and any LVGL
    // image cache entries that survived from the previous boot are still
    // valid. Warming both theme icons + the dashboard asset set here means
    // the rebuild's subsequent `lv_img_set_src` calls hit the cache rather
    // than triggering an FS open that would fail under the fragmented heap
    // we'll have AFTER the rebuild completes.
    IconAssets::preloadDashboardAssets();

    uint8_t savedIdx = s_currentIdx;

    // Load a blank screen so we can safely delete all page screens
    lv_obj_t *dummy = lv_obj_create(nullptr);
    lv_scr_load(dummy);

    // Destroy all existing page screens. Drop their widget entries from the
    // factory registry first — otherwise the registry leaks one full set of
    // entries per theme toggle and eventually overflows MAX_TRACKED_WIDGETS
    // (#57: "some gauges not rendered" after a few day/night switches).
    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (s_pages[i].screen) {
            WidgetFactory::clearAll(s_pages[i].screen);
            lv_obj_del(s_pages[i].screen);
            s_pages[i].screen = nullptr;
            s_pages[i].built = false;
        }
    }

    // Rebuild only the previously active page eagerly; all others are left
    // lazy (screen == nullptr) and will be constructed on first navigation.
    // This keeps at most one page in the LVGL pool during the rebuild.
    if (savedIdx < s_pageCount) {
        buildPage(savedIdx, dash.pages[s_pages[savedIdx].cfgIdx]);
    }

    // Return to the page that was active before the rebuild
    if (savedIdx < s_pageCount && s_pages[savedIdx].screen) {
        lv_scr_load(s_pages[savedIdx].screen);
        s_currentIdx = savedIdx;
    }

    lv_obj_del(dummy);

    // Re-warm the LVGL image cache BEFORE swapping the top bar icon. The
    // theme-toggle rebuild has just thrashed the image cache (each new
    // sensor icon evicts an older one), and the top bar's icon swap below
    // will trigger an FS open for the new day/night icon — which fails
    // under the heap fragmentation that follows the rebuild (#973). Re-
    // warming here uses the same heap-guarded preload path as boot, so it
    // bails gracefully when the pool is too starved instead of crashing.
    IconAssets::preloadDashboardAssets();

    // Update top bar colors for the new theme
    TopBar::reapplyTheme();

    LOG_INFO("UI", "Pages rebuilt for theme toggle");
}

// Deferred lazy-build callback — invoked by lv_async_call() at the start of
// the next lv_timer_handler() iteration, outside any event callback. This
// avoids the use-after-free that occurs when lv_obj_del(dep.screen) is called
// synchronously from within a button click handler: lv_obj_del fires
// LV_EVENT_DELETE on the button, which frees its tag, and the click handler
// then continues with a dangling pointer.
static void asyncDoLazyBuild(void * /*unused*/) {
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

// Page transitions on the ESP32:
//  - MOVE_LEFT/RIGHT: both screens translate together — old slides out while
//    new slides in. Visually similar to OVER_* for swipe nav but takes a
//    shorter duration to feel responsive (issue #95, F4 — cross-links #64).
//  - FADE_IN: alpha-blended cross-fade. Costly per pixel on a 320×240 panel
//    without a GPU; we keep it short. Used for programmatic navigation where
//    no direction is implied.
//
// Default swipe-transition duration (ms). Trimmed from 180 ms to 120 ms in
// fix F4 because the previous value dominated the page-switch wall time.
static constexpr uint32_t SWIPE_ANIM_MS = 120;

void showPage(uint8_t idx, lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_FADE_IN,
              uint32_t durationMs = 120) {
    if (idx >= s_pageCount) {
        LOG_WARN("UI", "showPage: idx=%u out of range (pageCount=%u)", idx, s_pageCount);
        return;
    }

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

// ---------------------------------------------------------------------------
// Gesture handling — extracted to gesture_controller (#704). PageManager
// receives swipe events via the callback registered in init().
// ---------------------------------------------------------------------------

void onSwipe(lv_dir_t dir) {
    if (s_pageCount <= 1)
        return;
    switch (dir) {
        case LV_DIR_LEFT:
            // Next page enters from the right, slides left — matches finger motion.
            showPage((s_currentIdx + 1) % s_pageCount, LV_SCR_LOAD_ANIM_MOVE_LEFT, SWIPE_ANIM_MS);
            LOG_VDEBUG("UI", "Gesture: swipe left → next page");
            break;
        case LV_DIR_RIGHT:
            showPage(s_currentIdx == 0 ? s_pageCount - 1 : s_currentIdx - 1,
                     LV_SCR_LOAD_ANIM_MOVE_RIGHT, SWIPE_ANIM_MS);
            LOG_VDEBUG("UI", "Gesture: swipe right → prev page");
            break;
        default:
            break;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PageManager::init() {
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
    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (strcmp(s_pages[i].id, pageId) == 0) {
            showPage(i);
            return true;
        }
    }
    LOG_WARN("UI", "Page not found: %s", pageId);
    return false;
}

bool PageManager::navigateToIndex(uint8_t index) {
    if (index >= s_pageCount)
        return false;
    showPage(index);
    return true;
}

void PageManager::navigateNext() {
    if (s_pageCount == 0)
        return;
    showPage((s_currentIdx + 1) % s_pageCount, LV_SCR_LOAD_ANIM_MOVE_LEFT, SWIPE_ANIM_MS);
}

void PageManager::navigatePrev() {
    if (s_pageCount == 0)
        return;
    showPage((s_currentIdx == 0) ? s_pageCount - 1 : s_currentIdx - 1, LV_SCR_LOAD_ANIM_MOVE_RIGHT,
             SWIPE_ANIM_MS);
}

const char *PageManager::getCurrentPageId() {
    if (s_pageCount == 0)
        return "";
    return s_pages[s_currentIdx].id;
}

const char *PageManager::getDefaultPageId() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    return dash.defaultPageId;
}

void PageManager::requestRebuild() {
    s_rebuildRequested = true;
}

void PageManager::requestReload() {
    s_reloadRequested = true;
}

void PageManager::updateWidgets() {
    // Reload supersedes a pending rebuild — the reload re-applies the theme
    // and rebuilds every page. Check it BEFORE the rebuild flag and clear
    // both so we don't double-rebuild on the next tick.
    if (s_reloadRequested) {
        s_reloadRequested = false;
        s_rebuildRequested = false;
        if (ConfigLoader::reloadAll()) {
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

    // Rebuild all pages when a theme switch has been requested
    if (s_rebuildRequested) {
        rebuildAllPages();
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
    if (!s_revOverlay)
        return;
    if (visible) {
        lv_obj_clear_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_revOverlay, LV_OBJ_FLAG_HIDDEN);
    }
}

uint8_t PageManager::getPageCount() {
    return s_pageCount;
}
