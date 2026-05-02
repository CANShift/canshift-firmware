// top_bar.cpp — Persistent top status bar

#include "top_bar.h"
#include "settings_page.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static lv_obj_t *s_bar = nullptr;
static lv_obj_t *s_mapLabel = nullptr;
static lv_obj_t *s_milIcon = nullptr;
static lv_obj_t *s_themeBtn = nullptr;   // Day/night toggle (only when hasDayTheme)
static lv_obj_t *s_themeLabel = nullptr; // Icon label inside theme button
static lv_obj_t *s_gearBtn = nullptr;
static lv_obj_t *s_gearLabel = nullptr;
static int16_t s_height = 24;

// Day-mode icon characters (Unicode sun/moon — rendered as UTF-8 in LVGL labels)
static constexpr char ICON_SUN[]  = "\xE2\x98\x80"; // ☀ U+2600
static constexpr char ICON_MOON[] = "\xE2\x98\xBE"; // ☾ U+263E

// Map name strings — MaxxECU may expose a numeric map index
// TODO: Expand with map name strings if MaxxECU exposes them
static const char *const MAP_NAMES[] = {"MAP 1", "MAP 2", "MAP 3", "MAP 4",
                                        "MAP 5", "MAP 6", "MAP 7", "MAP 8"};
static constexpr uint8_t MAP_NAME_COUNT = 8;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TopBar::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    const CfgTopBar &cfg = dash.topBar;
    s_height = cfg.height > 0 ? cfg.height : 24;

    // Create bar on the top layer so it always appears above page content
    s_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bar, LV_HOR_RES, s_height);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(cfg.bgColor.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_bar, 2, LV_PART_MAIN);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);

    // Map name label (left-aligned)
    if (cfg.showMapName) {
        s_mapLabel = lv_label_create(s_bar);
        lv_obj_align(s_mapLabel, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_set_style_text_color(s_mapLabel, lv_color_hex(cfg.textColor.rgb), 0);
        lv_obj_set_style_text_font(s_mapLabel, &lv_font_montserrat_12, 0);
        lv_label_set_text(s_mapLabel, "CANShift");
    }

    // MIL icon — offset from the right edge, leaving room for gear + optional theme button
    const int16_t milRightReserved = dash.hasDayTheme ? (s_height * 2 + 4) : (s_height + 4);
    s_milIcon = lv_obj_create(s_bar);
    lv_obj_set_size(s_milIcon, 8, 8);
    lv_obj_align(s_milIcon, LV_ALIGN_RIGHT_MID, -milRightReserved, 0);
    lv_obj_set_style_bg_color(s_milIcon, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_radius(s_milIcon, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_milIcon, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_milIcon, LV_OBJ_FLAG_HIDDEN); // Hidden by default

    // Gear / settings button — rightmost, toggles SettingsPage
    s_gearBtn = lv_btn_create(s_bar);
    lv_obj_set_size(s_gearBtn, s_height, s_height);
    lv_obj_align(s_gearBtn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_gearBtn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_gearBtn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_gearBtn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_gearBtn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_gearBtn, 0, LV_PART_MAIN);

    s_gearLabel = lv_label_create(s_gearBtn);
    lv_label_set_text(s_gearLabel, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(s_gearLabel, lv_color_hex(0x555555), 0);
    lv_obj_center(s_gearLabel);

    lv_obj_add_event_cb(
        s_gearBtn,
        [](lv_event_t * /*e*/) {
            bool nowOpen = SettingsPage::toggle();
            lv_label_set_text(s_gearLabel, nowOpen ? LV_SYMBOL_CLOSE : LV_SYMBOL_SETTINGS);
            lv_obj_set_style_text_color(s_gearLabel, lv_color_hex(nowOpen ? 0xCC3333 : 0x555555),
                                        0);
        },
        LV_EVENT_CLICKED, nullptr);

    // Day/night toggle button — only created when config has a day theme
    if (dash.hasDayTheme) {
        s_themeBtn = lv_btn_create(s_bar);
        lv_obj_set_size(s_themeBtn, s_height, s_height);
        // Place immediately left of the gear button
        lv_obj_align(s_themeBtn, LV_ALIGN_RIGHT_MID, -s_height, 0);
        lv_obj_set_style_bg_opa(s_themeBtn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_themeBtn, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_themeBtn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_themeBtn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_themeBtn, 0, LV_PART_MAIN);

        s_themeLabel = lv_label_create(s_themeBtn);
        lv_label_set_text(s_themeLabel, ThemeManager::isDayMode() ? ICON_MOON : ICON_SUN);
        lv_obj_set_style_text_color(s_themeLabel, lv_color_hex(cfg.textColor.rgb), 0);
        lv_obj_center(s_themeLabel);

        lv_obj_add_event_cb(
            s_themeBtn,
            [](lv_event_t * /*e*/) {
                ThemeManager::toggleDayMode();
                // Icon is updated in reapplyTheme(), called after rebuild completes
            },
            LV_EVENT_CLICKED, nullptr);
    }

    // Initialize settings page overlay (positioned directly below the top bar)
    SettingsPage::init(s_height, static_cast<int16_t>(LV_VER_RES - s_height));

    LOG_INFO("UI", "Top bar initialized (height=%dpx)", s_height);
}

void TopBar::reapplyTheme() {
    if (!s_bar)
        return;

    const CfgTopBar &cfg = ConfigLoader::getDashboardConfig().topBar;

    lv_obj_set_style_bg_color(s_bar, lv_color_hex(cfg.bgColor.rgb), LV_PART_MAIN);

    if (s_mapLabel) {
        lv_obj_set_style_text_color(s_mapLabel, lv_color_hex(cfg.textColor.rgb), 0);
    }

    // Update the theme toggle icon to reflect the new mode
    if (s_themeLabel) {
        lv_label_set_text(s_themeLabel, ThemeManager::isDayMode() ? ICON_MOON : ICON_SUN);
        lv_obj_set_style_text_color(s_themeLabel, lv_color_hex(cfg.textColor.rgb), 0);
    }
}

void TopBar::update() {
    // Update map name from signal store
    if (s_mapLabel) {
        float mapNum = SignalStore::read(SignalIds::MAP_NUMBER, 0.0f);
        bool mapValid = SignalStore::isValid(SignalIds::MAP_NUMBER);

        if (mapValid) {
            uint8_t idx = static_cast<uint8_t>(mapNum);
            if (idx > 0 && idx <= MAP_NAME_COUNT) {
                lv_label_set_text(s_mapLabel, MAP_NAMES[idx - 1]);
            } else {
                char buf[16];
                snprintf(buf, sizeof(buf), "MAP %u", idx);
                lv_label_set_text(s_mapLabel, buf);
            }
        } else {
            lv_label_set_text(s_mapLabel, "CANShift");
        }
    }

    // Update MIL indicator
    if (s_milIcon) {
        bool milActive = SignalStore::read(SignalIds::FLAG_MIL, 0.0f) > 0.5f;
        if (milActive) {
            lv_obj_clear_flag(s_milIcon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_milIcon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

int16_t TopBar::getHeight() {
    return s_height;
}
