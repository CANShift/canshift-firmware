// page_manager.cpp — Dashboard page lifecycle and navigation

#include "page_manager.h"
#include "widget_factory.h"
#include "top_bar.h"
#include "settings_page.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "runtime/alert_engine.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <string.h>

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

    applyPageBackground(p.screen, cfg);

    // Adjust content area for top bar
    int16_t contentY = cfg.showTopBar ? TopBar::getHeight() : 0;

    // Create all widgets for this page
    for (uint8_t w = 0; w < cfg.widgetCount; ++w) {
        const CfgWidget &wCfg = cfg.widgets[w];
        WidgetFactory::create(p.screen, wCfg, contentY);
    }

    p.built = true;
    LOG_DEBUG("UI", "Built page '%s' with %d widgets", cfg.id, cfg.widgetCount);
}

void showPage(uint8_t idx) {
    if (idx >= s_pageCount)
        return;

    lv_scr_load_anim(s_pages[idx].screen, LV_SCR_LOAD_ANIM_FADE_IN,
                     150,  // Animation duration ms
                     0,    // Delay ms
                     false // Don't delete old screen
    );

    s_currentIdx = idx;
    LOG_INFO("UI", "Navigated to page '%s'", s_pages[idx].id);
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
//   Swipe DOWN  → open settings panel (if closed)
//   Swipe UP    → close settings panel (if open)
//   Swipe LEFT  → next page           (only while settings is closed)
//   Swipe RIGHT → previous page       (only while settings is closed)

void onGesture(lv_dir_t dir) {
    if (SettingsPage::isOpen()) {
        if (dir == LV_DIR_TOP) {
            SettingsPage::close();
            LOG_DEBUG("UI", "Gesture: swipe up → settings closed");
        }
        // All other swipes are ignored while settings is visible
        return;
    }

    switch (dir) {
        case LV_DIR_BOTTOM:
            SettingsPage::open();
            LOG_DEBUG("UI", "Gesture: swipe down → settings opened");
            break;
        case LV_DIR_LEFT:
            if (s_pageCount > 1) {
                showPage((s_currentIdx + 1) % s_pageCount);
                LOG_DEBUG("UI", "Gesture: swipe left → next page");
            }
            break;
        case LV_DIR_RIGHT:
            if (s_pageCount > 1) {
                showPage(s_currentIdx == 0 ? s_pageCount - 1 : s_currentIdx - 1);
                LOG_DEBUG("UI", "Gesture: swipe right → prev page");
            }
            break;
        default:
            break;
    }
}

void checkGestures() {
    // Iterate all registered indev instances and look for a pointer device.
    // There is only one touch controller on this hardware.
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    while (indev != nullptr) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_dir_t dir = lv_indev_get_gesture_dir(indev);
            if (dir != LV_DIR_NONE) {
                onGesture(dir);
            }
            break;
        }
        indev = lv_indev_get_next(indev);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void PageManager::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();

    s_pageCount = 0;
    s_currentIdx = 0;

    if (!dash.loaded) {
        LOG_WARN("UI", "Dashboard config not loaded — creating placeholder page");
        // TODO: Create a minimal fallback "Config Error" page
        return;
    }

    // Initialize the top bar (persistent overlay, not part of any page)
    TopBar::init();

    // Build all pages
    for (uint8_t i = 0; i < dash.pageCount && i < MAX_PAGES; ++i) {
        buildPage(i, dash.pages[i]);
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
    navigateToIndex((s_currentIdx + 1) % s_pageCount);
}

void PageManager::navigatePrev() {
    navigateToIndex((s_currentIdx == 0) ? s_pageCount - 1 : s_currentIdx - 1);
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

void PageManager::updateWidgets() {
    if (s_pageCount == 0)
        return;

    // Process swipe gestures before widget updates so navigation changes take
    // effect on the same frame that LVGL renders.
    checkGestures();

    // Update widgets on the current page
    lv_obj_t *currentScreen = s_pages[s_currentIdx].screen;
    WidgetFactory::updateAll(currentScreen);

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
