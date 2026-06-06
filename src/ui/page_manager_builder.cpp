// page_manager_builder.cpp — LVGL page construction + day/night theme rebuild
//
// Split out of page_manager.cpp during the #1207 refactor. Owns:
//   - per-page LVGL widget tree construction (buildPage + applyPageBackground)
//   - the procedural cruise-control 2×2 button grid template (#451)
//   - the in-place theme reapply fast path (#1257)
//   - the destructive rebuildAllPages path used by USB CMD_PUT_CONFIG, with
//     the #1295 dummy-screen UAF guard preserved verbatim
//
// State (s_pages, s_currentIdx, s_rebuildRequested, s_pendingFreeIdx) is
// owned by page_manager.cpp; this TU reaches in via page_manager_internal.h.

#include "page_manager_internal.h"

#include "icon_assets.h"
#include "theme_manager.h"
#include "top_bar.h"
#include "widget_factory.h"

#include "config/config_loader.h"
#include "diag/logger.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string.h>

namespace PageManagerInternal {

namespace {

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

} // namespace

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

// Theme-toggle fast path (issue #1257). Walks every built page screen and
// re-applies the new day/night theme in place — no `lv_obj_del`, no
// `WidgetFactory::clearAll`, no SPIFFS icon reloads, no LVGL pool churn.
// Wall time is sub-millisecond instead of the hundred-or-so the destructive
// rebuild paid. `rebuildAllPages()` stays the path for `requestReload()`
// (PUT_CONFIG → real structural change).
void reapplyThemeAllPages() {
    s_rebuildRequested = false;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || s_pageCount == 0)
        return;

    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (!s_pages[i].screen)
            continue;
        const CfgPage &cfg = dash.pages[s_pages[i].cfgIdx];
        const CfgColor effectiveBg = ThemeManager::getEffectiveBgColor(cfg.bgColor);
        lv_obj_set_style_bg_color(s_pages[i].screen, lv_color_hex(effectiveBg.rgb), LV_PART_MAIN);
        WidgetFactory::reapplyTheme(s_pages[i].screen);
    }

    TopBar::reapplyTheme();
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

    // Return to the page that was active before the rebuild, then delete the
    // dummy. Order matters: `lv_obj_del(dummy)` MUST run only after a real
    // page screen has been loaded as the active screen — deleting the active
    // screen crashes LVGL (#1284). If `buildPage` above failed (e.g. heap
    // exhaustion, font load), try the first page that survives a rebuild as
    // a fallback. If every page fails, keep the dummy as the active screen
    // — visually awkward but not a crash — and surface the failure.
    uint8_t loadedIdx = 0xFF;
    if (savedIdx < s_pageCount && s_pages[savedIdx].screen) {
        loadedIdx = savedIdx;
    } else {
        for (uint8_t i = 0; i < s_pageCount; ++i) {
            if (i == savedIdx)
                continue; // already attempted above
            buildPage(i, dash.pages[s_pages[i].cfgIdx]);
            if (s_pages[i].screen) {
                loadedIdx = i;
                break;
            }
        }
    }

    if (loadedIdx != 0xFF) {
        lv_scr_load(s_pages[loadedIdx].screen);
        s_currentIdx = loadedIdx;
        lv_obj_del(dummy);
    } else {
        // Do NOT delete the dummy — it's still the active screen. Leaving it
        // alive avoids the UAF; the blank screen is the visible symptom of a
        // catastrophic rebuild failure (every page failed to build).
        LOG_ERROR("UI", "rebuildAllPages: no page could be rebuilt — staging "
                        "screen kept active to avoid LVGL UAF");
    }

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

} // namespace PageManagerInternal
