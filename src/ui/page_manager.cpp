// page_manager.cpp — Dashboard page lifecycle and navigation

#include "page_manager.h"
#include "ui/font_manager.h"
#include "ui/burn_overlay.h"
#include "widget_factory.h"
#include "top_bar.h"
#include "error_bar.h"
#include "settings_page.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "runtime/alert_engine.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"
#include "app_config.h"

#include <lvgl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

namespace {

static constexpr uint8_t MAX_PAGES = CONFIG_MAX_PAGES;

struct Page {
    char id[CFG_MAX_ID_LEN];
    lv_obj_t *screen; // LVGL screen object for this page
    bool built;
};

static Page s_pages[MAX_PAGES];
static uint8_t s_pageCount = 0;
static uint8_t s_currentIdx = 0;
static lv_obj_t *s_revOverlay = nullptr; // Red flash overlay, global
static bool s_rebuildRequested = false;  // Set by ThemeManager::toggleDayMode()
static bool s_reloadRequested = false;   // Set by USB CMD_PUT_CONFIG handler

void applyPageBackground(lv_obj_t *screen, const CfgPage &cfg) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(cfg.bgColor.rgb), LV_PART_MAIN);
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
    Page &p = s_pages[idx];
    strlcpy(p.id, cfg.id, CFG_MAX_ID_LEN);

    // Create an LVGL screen for each page
    p.screen = lv_obj_create(nullptr); // nullptr = new screen
    lv_obj_set_size(p.screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(p.screen, LV_OBJ_FLAG_SCROLLABLE);

    // Apply theme-aware background (day vs night)
    CfgPage effectiveCfg = cfg;
    effectiveCfg.bgColor = ThemeManager::getEffectiveBgColor(cfg.bgColor);
    applyPageBackground(p.screen, effectiveCfg);

    // Adjust content area for top bar
    int16_t contentY = cfg.showTopBar ? TopBar::getHeight() : 0;

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
        LOG_WARN("UI", "Page '%s': only %u/%u widgets built — see prior WF errors",
                 cfg.id, static_cast<unsigned>(created),
                 static_cast<unsigned>(cfg.widgetCount));
    } else {
        LOG_INFO("UI", "Built page '%s' with %u widgets", cfg.id,
                 static_cast<unsigned>(cfg.widgetCount));
    }
}

void rebuildAllPages() {
    s_rebuildRequested = false;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || s_pageCount == 0)
        return;

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

    // Rebuild with the active theme colors. Skip hidden pages so the
    // visible-page index space stays in sync with init() (issue #204).
    uint8_t outIdx = 0;
    for (uint8_t i = 0; i < dash.pageCount && outIdx < s_pageCount; ++i) {
        if (!dash.pages[i].visible)
            continue;
        buildPage(outIdx, dash.pages[i]);
        ++outIdx;
    }

    // Return to the page that was active before the rebuild
    if (savedIdx < s_pageCount && s_pages[savedIdx].screen) {
        lv_scr_load(s_pages[savedIdx].screen);
        s_currentIdx = savedIdx;
    }

    lv_obj_del(dummy);

    // Update top bar colors for the new theme
    TopBar::reapplyTheme();

    LOG_INFO("UI", "Pages rebuilt for theme toggle");
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
    if (!s_pages[idx].screen) {
        LOG_ERROR("UI", "showPage: page '%s' has no screen object", s_pages[idx].id);
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
    if (t) lv_timer_set_repeat_count(t, 1);
#endif

    s_currentIdx = idx;
    LOG_INFO("UI", "Navigated to page '%s' (idx=%u)", s_pages[idx].id, idx);
}

// ---------------------------------------------------------------------------
// Gesture handling
// ---------------------------------------------------------------------------
//
// LVGL 8.3 gesture recognition lives in the indev layer, not in the object
// event system. Reading lv_indev_get_gesture_dir() here (after lv_task_handler
// has run) is the reliable path — it works even when buttons or sliders absorb
// the touch event and prevent LV_EVENT_GESTURE from reaching the screen object.
//
// Gesture map:
//   Drag DOWN from top edge   → reveal settings panel (panel follows finger,
//                                snap-open / snap-closed at 50% threshold)
//   Drag UP   from top bar    → conceal settings panel (same model, reversed)
//   Swipe LEFT                → next page           (only while settings closed)
//   Swipe RIGHT               → previous page       (only while settings closed)
//
// The drag tracker takes priority over the LVGL swipe-gesture path: while a
// drag is in progress the swipe handler is suppressed for that touch cycle so
// we don't both translate the panel and fire a second open/close at release.

// y-coordinate band that triggers the drag from the closed state. Picked to
// be a hair larger than the top bar so a finger landing on the bar can still
// initiate the gesture without needing pixel-perfect aim.
static constexpr int16_t DRAG_HOTZONE_PX = 40;

// Movement (in px) below which a press isn't a drag. Below the LVGL gesture
// threshold so we settle ambiguity before the swipe path fires.
static constexpr int16_t DRAG_START_THRESHOLD_PX = 6;

void onGesture(lv_dir_t dir) {
    // Settings drag is handled by the drag tracker — ignore swipe gestures
    // when the panel is open or in motion so we don't double-fire close().
    if (SettingsPage::isOpen() || SettingsPage::isDragging())
        return;

    switch (dir) {
        case LV_DIR_LEFT:
            if (s_pageCount > 1) {
                // Next page enters from the right, slides left — matches finger motion.
                showPage((s_currentIdx + 1) % s_pageCount, LV_SCR_LOAD_ANIM_MOVE_LEFT,
                         SWIPE_ANIM_MS);
                LOG_DEBUG("UI", "Gesture: swipe left → next page");
            }
            break;
        case LV_DIR_RIGHT:
            if (s_pageCount > 1) {
                showPage(s_currentIdx == 0 ? s_pageCount - 1 : s_currentIdx - 1,
                         LV_SCR_LOAD_ANIM_MOVE_RIGHT, SWIPE_ANIM_MS);
                LOG_DEBUG("UI", "Gesture: swipe right → prev page");
            }
            break;
        case LV_DIR_TOP:
        case LV_DIR_BOTTOM:
        default:
            // Vertical gestures are owned by the drag tracker — see updateDrag().
            break;
    }
}

// Drag tracker state — reset on every touch release.
struct DragState {
    bool active = false;     // True between press and release.
    bool tracking = false;   // True once we've decided this is our gesture.
    int16_t startY = 0;      // Touch y where the press began.
    int16_t startPanelY = 0; // s_panel y at gesture start (open or closed Y).
};

static DragState s_drag;

void resetDrag() {
    s_drag = DragState{};
    SettingsPage::setDragging(false);
}

// Crossed-threshold flag — written by updateDrag() while the gesture is in
// flight, read by onDragRelease() to decide which way to snap. File-scope
// static so it shares the anonymous namespace with the rest of the tracker.
static bool s_dragCrossedThreshold = false;

void onDragRelease() {
    if (!s_drag.tracking) {
        resetDrag();
        return;
    }

    // Snap based on the issue spec ("drag > 50% of panel height"):
    //  - Started closed: cross midpoint downward → open, else fall back closed.
    //  - Started open:   cross midpoint upward   → close, else fall back open.
    const int16_t closedY = SettingsPage::getClosedY();
    const bool startedClosed = (s_drag.startPanelY == closedY);

    if (startedClosed) {
        if (s_dragCrossedThreshold)
            SettingsPage::snapOpen();
        else
            SettingsPage::snapClosed();
    } else {
        if (s_dragCrossedThreshold)
            SettingsPage::snapClosed();
        else
            SettingsPage::snapOpen();
    }

    resetDrag();
}

void updateDrag(lv_indev_t *indev, lv_indev_state_t state) {
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (state == LV_INDEV_STATE_RELEASED) {
        if (s_drag.active)
            onDragRelease();
        return;
    }

    // Pressed.
    if (!s_drag.active) {
        s_drag.active = true;
        s_drag.tracking = false;
        s_drag.startY = p.y;
        s_dragCrossedThreshold = false;

        const bool isOpen = SettingsPage::isOpen();
        // The drag is only armed when the press starts in the top hot-zone.
        // From closed, this is the band just under the top edge; from open,
        // it's the top bar itself — both at p.y < DRAG_HOTZONE_PX. Anywhere
        // else inside the open panel keeps its normal click/scroll behavior.
        if (p.y > DRAG_HOTZONE_PX) {
            s_drag.active = false;
            return; // Not our gesture.
        }

        s_drag.startPanelY = isOpen ? SettingsPage::getOpenY() : SettingsPage::getClosedY();
    }

    if (!s_drag.active)
        return;

    const int16_t deltaY = p.y - s_drag.startY;

    // Latch into "tracking" only once the finger has moved enough — below the
    // threshold the press is treated as a tap so buttons / sliders inside the
    // panel still work normally when Settings is open.
    if (!s_drag.tracking) {
        if (abs(deltaY) < DRAG_START_THRESHOLD_PX)
            return;

        const bool isOpen = SettingsPage::isOpen();
        // From closed, only downward drags qualify; from open, only upward.
        if (!isOpen && deltaY <= 0)
            return;
        if (isOpen && deltaY >= 0)
            return;

        s_drag.tracking = true;
        SettingsPage::setDragging(true);
        LOG_DEBUG("UI", "Drag: settings tracker armed (open=%d)", static_cast<int>(isOpen));
    }

    // Translate panel position 1:1 with the finger.
    int16_t targetY = static_cast<int16_t>(s_drag.startPanelY + deltaY);
    SettingsPage::setPanelY(targetY);

    // Threshold = midpoint between closed and open. Once crossed in either
    // direction we remember it for the release decision.
    const int16_t closedY = SettingsPage::getClosedY();
    const int16_t openY = SettingsPage::getOpenY();
    const int16_t midpointY = closedY + (openY - closedY) / 2;
    const bool startedClosed = (s_drag.startPanelY == closedY);
    if (startedClosed) {
        // Crossed = pulled past midpoint (user wants to open).
        s_dragCrossedThreshold = (targetY >= midpointY);
    } else {
        // Crossed = pushed past midpoint upward (user wants to close).
        s_dragCrossedThreshold = (targetY <= midpointY);
    }
}

void checkGestures() {
    // gesture_dir stays armed until a new gesture is detected — without this
    // tracker the same swipe re-fires every UI tick (#40 page-flip loop).
    // We consume it once per touch cycle and reset on physical release.
    static lv_dir_t lastDir = LV_DIR_NONE;

    lv_indev_t *indev = lv_indev_get_next(nullptr);
    while (indev != nullptr) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            // LVGL 8.3 exposes the state via the proc struct, not a getter.
            const lv_indev_state_t state = indev->proc.state;

            // Drag-to-reveal runs every tick — it owns the press/move/release
            // cycle for the settings panel.
            updateDrag(indev, state);

            if (state == LV_INDEV_STATE_RELEASED) {
                lastDir = LV_DIR_NONE;
            } else {
                lv_dir_t dir = lv_indev_get_gesture_dir(indev);
                if (dir != LV_DIR_NONE && dir != lastDir) {
                    onGesture(dir);
                    lastDir = dir;
                }
            }
            break;
        }
        indev = lv_indev_get_next(indev);
    }
}

// ---------------------------------------------------------------------------
// Setup screen — shown when no dashboard.json is present
// ---------------------------------------------------------------------------

static void animBreath(void *obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
}

void showSetupScreen() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0D0D0D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ---------- Logo ----------
    lv_obj_t *logo = lv_label_create(scr);
    lv_label_set_text(logo, "CANShift");
    lv_obj_set_style_text_font(logo, FontManager::primary(32), 0);
    lv_obj_set_style_text_color(logo, lv_color_hex(0xFF4444), 0);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 28);

    // ---------- Version ----------
    char verBuf[16];
    snprintf(verBuf, sizeof(verBuf), "v" APP_VERSION_STR);
    lv_obj_t *ver = lv_label_create(scr);
    lv_label_set_text(ver, verBuf);
    lv_obj_set_style_text_font(ver, FontManager::label(12), 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0x444444), 0);
    lv_obj_align_to(ver, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    // ---------- Separator ----------
    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, 200, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);
    lv_obj_align(sep, LV_ALIGN_CENTER, 0, -28);

    // ---------- "Ready to configure" ----------
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Ready to configure");
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -8);

    // ---------- Instruction ----------
    lv_obj_t *instr = lv_label_create(scr);
    lv_label_set_text(instr, "Open CANShift Studio and connect\nthis device via USB.");
    lv_obj_set_style_text_font(instr, FontManager::label(12), 0);
    lv_obj_set_style_text_color(instr, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_align(instr, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(instr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(instr, LV_HOR_RES - 40);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, 26);

    // ---------- Pulsing dot — "waiting" ----------
    lv_obj_t *dot = lv_label_create(scr);
    lv_label_set_text(dot, "\xE2\x97\x8F"); // ● filled circle
    lv_obj_set_style_text_font(dot, FontManager::label(16), 0);
    lv_obj_set_style_text_color(dot, lv_color_hex(0xFF4444), 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -24);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, animBreath);
    lv_anim_set_var(&a, dot);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_time(&a, 900);
    lv_anim_set_playback_time(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_scr_load(scr);
    LOG_INFO("UI", "Setup screen shown — waiting for Studio connection");
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PageManager::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();

    s_pageCount = 0;
    s_currentIdx = 0;

    // Init error bar first so errors pushed during boot (config load, CAN init)
    // are visible regardless of whether a valid dashboard config exists.
    ErrorBar::init();

    if (!dash.loaded) {
        LOG_WARN("UI", "No dashboard config — showing setup screen");
        showSetupScreen();
        return;
    }

    // Load persisted day/night preference before building pages
    ThemeManager::init();

    // Initialize the top bar (persistent overlay, not part of any page)
    TopBar::init();

    // Build only visible pages — hidden pages stay in the studio config but
    // are excluded from device iteration / navigation (issue #204).
    for (uint8_t i = 0; i < dash.pageCount && s_pageCount < MAX_PAGES; ++i) {
        if (!dash.pages[i].visible) {
            LOG_INFO("UI", "Skipping hidden page '%s' (visible=false)", dash.pages[i].id);
            continue;
        }
        buildPage(s_pageCount, dash.pages[i]);
        s_pageCount++;
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
    if (s_pageCount == 0) return;
    showPage((s_currentIdx + 1) % s_pageCount, LV_SCR_LOAD_ANIM_MOVE_LEFT, SWIPE_ANIM_MS);
}

void PageManager::navigatePrev() {
    if (s_pageCount == 0) return;
    showPage((s_currentIdx == 0) ? s_pageCount - 1 : s_currentIdx - 1,
             LV_SCR_LOAD_ANIM_MOVE_RIGHT, SWIPE_ANIM_MS);
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
    checkGestures();

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

    SettingsPage::tickSleep();
    ErrorBar::update();
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
