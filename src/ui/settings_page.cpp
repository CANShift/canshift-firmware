// settings_page.cpp — On-device LVGL screen settings page

#include "settings_page.h"
#include "hal/display/display_driver.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <Preferences.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// NVS namespace and key names
// ---------------------------------------------------------------------------

static constexpr char NVS_NS[] = "screen_cfg";
static constexpr char KEY_BRIGHTNESS[] = "brightness"; // uint8  (10–100 %)
static constexpr char KEY_CONTRAST[] = "contrast";     // uint8  (0–100 %)
static constexpr char KEY_SLEEP_S[] = "sleep_s";       // uint32 (0/30/60/300)
static constexpr char KEY_ROTATION[] = "rotation";     // uint8  (0/90/180/270)

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

static constexpr uint8_t DEFAULT_BRIGHTNESS = 80;
static constexpr uint8_t DEFAULT_CONTRAST = 50;
static constexpr uint32_t DEFAULT_SLEEP_S = 0;
static constexpr uint8_t DEFAULT_ROTATION = 0;

// ---------------------------------------------------------------------------
// Sleep timeout options
// ---------------------------------------------------------------------------

static constexpr uint8_t SLEEP_OPTION_COUNT = 4;
static constexpr uint32_t SLEEP_OPTIONS[SLEEP_OPTION_COUNT] = {0, 30, 60, 300};
static const char *const SLEEP_LABELS[SLEEP_OPTION_COUNT] = {"Off", "30s", "1m", "5m"};

// ---------------------------------------------------------------------------
// Rotation options
// ---------------------------------------------------------------------------

static constexpr uint8_t ROT_OPTION_COUNT = 4;
static constexpr uint16_t ROT_OPTIONS[ROT_OPTION_COUNT] = {0, 90, 180, 270};

// ---------------------------------------------------------------------------
// Colors (hex, no alpha prefix needed for lv_color_hex)
// ---------------------------------------------------------------------------

static constexpr uint32_t CLR_BG = 0x0D0D0D;
static constexpr uint32_t CLR_ACCENT = 0xCC3333;
static constexpr uint32_t CLR_TEXT = 0xCCCCCC;
static constexpr uint32_t CLR_MUTED = 0x555555;
static constexpr uint32_t CLR_BTN_BG = 0x111111;
static constexpr uint32_t CLR_BTN_ACT = 0x1A0A0A;
static constexpr uint32_t CLR_BTN_BDR = 0x2A2A2A;
static constexpr uint32_t CLR_SAVE_BG = 0x1A3A1A;
static constexpr uint32_t CLR_SAVE_BDR = 0x336633;
static constexpr uint32_t CLR_SAVE_TEXT = 0x55AA55;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

namespace {

// Current settings (in-memory)
static uint8_t s_brightness = DEFAULT_BRIGHTNESS;
static uint8_t s_contrast = DEFAULT_CONTRAST;
static uint32_t s_sleepTimeoutS = DEFAULT_SLEEP_S;
static uint16_t s_rotation = DEFAULT_ROTATION;

// LVGL objects
static lv_obj_t *s_panel = nullptr; // Root overlay container
static lv_obj_t *s_brSlider = nullptr;
static lv_obj_t *s_brValue = nullptr; // "80%" label
static lv_obj_t *s_ctSlider = nullptr;
static lv_obj_t *s_ctValue = nullptr;
static lv_obj_t *s_sleepBtns[SLEEP_OPTION_COUNT] = {};
static lv_obj_t *s_rotBtns[ROT_OPTION_COUNT] = {};

// True when settings overlay is visible
static bool s_open = false;

// -----------------------------------------------------------------------
// NVS helpers
// -----------------------------------------------------------------------

void nvsLoad() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    s_brightness = p.getUChar(KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    s_contrast = p.getUChar(KEY_CONTRAST, DEFAULT_CONTRAST);
    s_sleepTimeoutS = p.getUInt(KEY_SLEEP_S, DEFAULT_SLEEP_S);
    s_rotation = p.getUChar(KEY_ROTATION, DEFAULT_ROTATION);
    p.end();

    // Clamp to valid ranges
    if (s_brightness < 10 || s_brightness > 100)
        s_brightness = DEFAULT_BRIGHTNESS;
    if (s_contrast > 100)
        s_contrast = DEFAULT_CONTRAST;
}

void nvsSave() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putUChar(KEY_BRIGHTNESS, s_brightness);
    p.putUChar(KEY_CONTRAST, s_contrast);
    p.putUInt(KEY_SLEEP_S, s_sleepTimeoutS);
    p.putUChar(KEY_ROTATION, s_rotation);
    p.end();
    LOG_INFO("Settings", "Saved — brightness=%d%% contrast=%d%% sleep=%ds rotation=%d",
             s_brightness, s_contrast, s_sleepTimeoutS, s_rotation);
}

// -----------------------------------------------------------------------
// Apply helpers
// -----------------------------------------------------------------------

// Map brightness percentage (10–100) to LEDC duty (0–255)
inline uint8_t brightnessToBacklight(uint8_t pct) {
    return static_cast<uint8_t>((static_cast<uint16_t>(pct) * 255u) / 100u);
}

void applyBrightness() {
    DisplayDriver::setBacklight(brightnessToBacklight(s_brightness));
}

void updateBrValue() {
    if (!s_brValue)
        return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", s_brightness);
    lv_label_set_text(s_brValue, buf);
}

void updateCtValue() {
    if (!s_ctValue)
        return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", s_contrast);
    lv_label_set_text(s_ctValue, buf);
}

void updateSleepButtons() {
    for (uint8_t i = 0; i < SLEEP_OPTION_COUNT; ++i) {
        if (!s_sleepBtns[i])
            continue;
        bool active = (SLEEP_OPTIONS[i] == s_sleepTimeoutS);
        lv_obj_set_style_bg_color(s_sleepBtns[i], lv_color_hex(active ? CLR_BTN_ACT : CLR_BTN_BG),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_color(
            s_sleepBtns[i], lv_color_hex(active ? CLR_ACCENT : CLR_BTN_BDR), LV_PART_MAIN);
        lv_obj_t *lbl = lv_obj_get_child(s_sleepBtns[i], 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(active ? CLR_ACCENT : CLR_MUTED), 0);
        }
    }
}

void updateRotButtons() {
    for (uint8_t i = 0; i < ROT_OPTION_COUNT; ++i) {
        if (!s_rotBtns[i])
            continue;
        bool active = (ROT_OPTIONS[i] == s_rotation);
        lv_obj_set_style_bg_color(s_rotBtns[i], lv_color_hex(active ? CLR_BTN_ACT : CLR_BTN_BG),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_color(s_rotBtns[i], lv_color_hex(active ? CLR_ACCENT : CLR_BTN_BDR),
                                      LV_PART_MAIN);
        lv_obj_t *lbl = lv_obj_get_child(s_rotBtns[i], 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(active ? CLR_ACCENT : CLR_MUTED), 0);
        }
    }
}

// -----------------------------------------------------------------------
// Event callbacks
// -----------------------------------------------------------------------

static void onBrightnessChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    s_brightness = static_cast<uint8_t>(lv_slider_get_value(slider));
    updateBrValue();
    applyBrightness(); // Live preview
}

static void onContrastChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    s_contrast = static_cast<uint8_t>(lv_slider_get_value(slider));
    updateCtValue();
    // TODO: Apply contrast hardware control when available
}

static void onSleepBtn(lv_event_t *e) {
    uint32_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (idx >= SLEEP_OPTION_COUNT)
        return;
    s_sleepTimeoutS = SLEEP_OPTIONS[idx];
    updateSleepButtons();
    // TODO: Start/reset sleep timer in power management module
}

static void onRotBtn(lv_event_t *e) {
    uint32_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (idx >= ROT_OPTION_COUNT)
        return;
    s_rotation = ROT_OPTIONS[idx];
    updateRotButtons();
    // TODO: lv_disp_set_rotation() — requires reinit, applied on next boot
    // For now, store and apply on next boot via NVS
}

static void onSave(lv_event_t * /*e*/) {
    nvsSave();
}

static void onReset(lv_event_t * /*e*/) {
    s_brightness = DEFAULT_BRIGHTNESS;
    s_contrast = DEFAULT_CONTRAST;
    s_sleepTimeoutS = DEFAULT_SLEEP_S;
    s_rotation = DEFAULT_ROTATION;

    lv_slider_set_value(s_brSlider, s_brightness, LV_ANIM_OFF);
    lv_slider_set_value(s_ctSlider, s_contrast, LV_ANIM_OFF);
    updateBrValue();
    updateCtValue();
    updateSleepButtons();
    updateRotButtons();
    applyBrightness();
}

// -----------------------------------------------------------------------
// Layout helpers
// -----------------------------------------------------------------------

static const lv_font_t *FONT_LG = &lv_font_montserrat_12;
static const lv_font_t *FONT_SM = &lv_font_montserrat_10;

// Horizontal padding inside the panel
static constexpr int16_t PAD_H = 8;

lv_obj_t *makeRow(lv_obj_t *parent, int16_t y, int16_t h, const char *label,
                  lv_obj_t **valueLabel) {
    int16_t panelW = lv_obj_get_width(parent);
    int16_t rowW = panelW - PAD_H * 2;

    // Row container
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_pos(row, PAD_H, y);
    lv_obj_set_size(row, rowW, h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Label (left)
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, FONT_SM, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    // Value (right)
    if (valueLabel) {
        lv_obj_t *val = lv_label_create(row);
        lv_label_set_text(val, "");
        lv_obj_set_style_text_font(val, FONT_SM, 0);
        lv_obj_set_style_text_color(val, lv_color_hex(CLR_TEXT), 0);
        lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, 0);
        *valueLabel = val;
    }

    return row;
}

lv_obj_t *makeSlider(lv_obj_t *parent, int32_t vmin, int32_t vmax, int32_t initial,
                     lv_event_cb_t cb) {
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_width(slider, lv_obj_get_width(parent));
    lv_slider_set_range(slider, vmin, vmax);
    lv_slider_set_value(slider, initial, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(slider, lv_color_hex(CLR_BTN_BDR), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(CLR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(CLR_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 3, LV_PART_KNOB);

    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return slider;
}

lv_obj_t *makeSegButton(lv_obj_t *parent, const char *label, bool active, lv_event_cb_t cb,
                        void *userData) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? CLR_BTN_ACT : CLR_BTN_BG), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(active ? CLR_ACCENT : CLR_BTN_BDR),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, FONT_SM, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(active ? CLR_ACCENT : CLR_MUTED), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
    return btn;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SettingsPage::init(int16_t yOffset, int16_t height) {
    nvsLoad();

    const int16_t panelW = LV_HOR_RES;

    // Root overlay — sibling of top bar on lv_layer_top(), drawn above it
    s_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(s_panel, 0, yOffset);
    lv_obj_set_size(s_panel, panelW, height);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    // ---- Layout constants ----
    const int16_t rowW = panelW - PAD_H * 2;
    const int16_t sliderH = 12;
    const int16_t labelH = 12;
    const int16_t btnH = 22;
    const int16_t gapRow = 6;
    const int16_t gapInner = 4;

    int16_t y = 6;

    // ---- Header ----
    {
        lv_obj_t *title = lv_label_create(s_panel);
        lv_label_set_text(title, "SCREEN SETTINGS");
        lv_obj_set_style_text_font(title, FONT_LG, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), 0);
        lv_obj_set_pos(title, PAD_H, y);
        y += 18;
    }

    // ---- Brightness ----
    y += gapRow;
    {
        // Header row: "BRIGHTNESS" + "80%"
        lv_obj_t *row = lv_obj_create(s_panel);
        lv_obj_set_pos(row, PAD_H, y);
        lv_obj_set_size(row, rowW, labelH);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, "BRIGHTNESS");
        lv_obj_set_style_text_font(lbl, FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        s_brValue = lv_label_create(row);
        lv_obj_set_style_text_font(s_brValue, FONT_SM, 0);
        lv_obj_set_style_text_color(s_brValue, lv_color_hex(CLR_TEXT), 0);
        lv_obj_align(s_brValue, LV_ALIGN_RIGHT_MID, 0, 0);
        updateBrValue();

        y += labelH + gapInner;

        s_brSlider = makeSlider(s_panel, 10, 100, s_brightness, onBrightnessChanged);
        lv_obj_set_pos(s_brSlider, PAD_H, y);
        lv_obj_set_size(s_brSlider, rowW, sliderH);
        y += sliderH;
    }

    // ---- Contrast ----
    y += gapRow;
    {
        lv_obj_t *row = lv_obj_create(s_panel);
        lv_obj_set_pos(row, PAD_H, y);
        lv_obj_set_size(row, rowW, labelH);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, "CONTRAST");
        lv_obj_set_style_text_font(lbl, FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        s_ctValue = lv_label_create(row);
        lv_obj_set_style_text_font(s_ctValue, FONT_SM, 0);
        lv_obj_set_style_text_color(s_ctValue, lv_color_hex(CLR_TEXT), 0);
        lv_obj_align(s_ctValue, LV_ALIGN_RIGHT_MID, 0, 0);
        updateCtValue();

        y += labelH + gapInner;

        s_ctSlider = makeSlider(s_panel, 0, 100, s_contrast, onContrastChanged);
        lv_obj_set_pos(s_ctSlider, PAD_H, y);
        lv_obj_set_size(s_ctSlider, rowW, sliderH);
        y += sliderH;
    }

    // ---- Sleep timeout ----
    y += gapRow;
    {
        lv_obj_t *lbl = lv_label_create(s_panel);
        lv_label_set_text(lbl, "SLEEP");
        lv_obj_set_style_text_font(lbl, FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_pos(lbl, PAD_H, y);
        y += labelH + gapInner;

        const int16_t gap = 4;
        const int16_t btnW = (rowW - gap * (SLEEP_OPTION_COUNT - 1)) / SLEEP_OPTION_COUNT;
        for (uint8_t i = 0; i < SLEEP_OPTION_COUNT; ++i) {
            bool active = (SLEEP_OPTIONS[i] == s_sleepTimeoutS);
            s_sleepBtns[i] = makeSegButton(s_panel, SLEEP_LABELS[i], active, onSleepBtn,
                                           reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
            lv_obj_set_pos(s_sleepBtns[i], PAD_H + i * (btnW + gap), y);
            lv_obj_set_size(s_sleepBtns[i], btnW, btnH);
        }
        y += btnH;
    }

    // ---- Rotation ----
    y += gapRow;
    {
        lv_obj_t *lbl = lv_label_create(s_panel);
        lv_label_set_text(lbl, "ROTATION");
        lv_obj_set_style_text_font(lbl, FONT_SM, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_pos(lbl, PAD_H, y);
        y += labelH + gapInner;

        const int16_t gap = 4;
        const int16_t btnW = (rowW - gap * (ROT_OPTION_COUNT - 1)) / ROT_OPTION_COUNT;
        char rotLabel[6];
        for (uint8_t i = 0; i < ROT_OPTION_COUNT; ++i) {
            snprintf(rotLabel, sizeof(rotLabel), "%d\xC2\xB0", ROT_OPTIONS[i]); // UTF-8 °
            bool active = (ROT_OPTIONS[i] == s_rotation);
            s_rotBtns[i] = makeSegButton(s_panel, rotLabel, active, onRotBtn,
                                         reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
            lv_obj_set_pos(s_rotBtns[i], PAD_H + i * (btnW + gap), y);
            lv_obj_set_size(s_rotBtns[i], btnW, btnH);
        }
        y += btnH;
    }

    // ---- Actions — pinned to bottom ----
    {
        const int16_t actionY = height - 8 - btnH;
        const int16_t gap = 6;
        const int16_t resetW = (rowW - gap) / 3;
        const int16_t saveW = rowW - resetW - gap;

        // RESET button
        lv_obj_t *resetBtn = lv_btn_create(s_panel);
        lv_obj_set_pos(resetBtn, PAD_H, actionY);
        lv_obj_set_size(resetBtn, resetW, btnH);
        lv_obj_set_style_bg_color(resetBtn, lv_color_hex(CLR_BTN_BG), LV_PART_MAIN);
        lv_obj_set_style_border_color(resetBtn, lv_color_hex(CLR_BTN_BDR), LV_PART_MAIN);
        lv_obj_set_style_border_width(resetBtn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(resetBtn, 3, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(resetBtn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(resetBtn, 3, LV_PART_MAIN);
        lv_obj_t *resetLbl = lv_label_create(resetBtn);
        lv_label_set_text(resetLbl, "RESET");
        lv_obj_set_style_text_font(resetLbl, FONT_SM, 0);
        lv_obj_set_style_text_color(resetLbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_center(resetLbl);
        lv_obj_add_event_cb(resetBtn, onReset, LV_EVENT_CLICKED, nullptr);

        // SAVE button
        lv_obj_t *saveBtn = lv_btn_create(s_panel);
        lv_obj_set_pos(saveBtn, PAD_H + resetW + gap, actionY);
        lv_obj_set_size(saveBtn, saveW, btnH);
        lv_obj_set_style_bg_color(saveBtn, lv_color_hex(CLR_SAVE_BG), LV_PART_MAIN);
        lv_obj_set_style_border_color(saveBtn, lv_color_hex(CLR_SAVE_BDR), LV_PART_MAIN);
        lv_obj_set_style_border_width(saveBtn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(saveBtn, 3, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(saveBtn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(saveBtn, 3, LV_PART_MAIN);
        lv_obj_t *saveLbl = lv_label_create(saveBtn);
        lv_label_set_text(saveLbl, "SAVE");
        lv_obj_set_style_text_font(saveLbl, FONT_SM, 0);
        lv_obj_set_style_text_color(saveLbl, lv_color_hex(CLR_SAVE_TEXT), 0);
        lv_obj_center(saveLbl);
        lv_obj_add_event_cb(saveBtn, onSave, LV_EVENT_CLICKED, nullptr);
    }

    applyBrightness();
    LOG_INFO("Settings", "Settings page initialized");
}

void SettingsPage::open() {
    if (!s_panel || s_open)
        return;
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
    s_open = true;
    LOG_DEBUG("Settings", "Settings page opened");
}

void SettingsPage::close() {
    if (!s_panel || !s_open)
        return;
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
    LOG_DEBUG("Settings", "Settings page closed");
}

bool SettingsPage::toggle() {
    if (s_open) {
        close();
    } else {
        open();
    }
    return s_open;
}

bool SettingsPage::isOpen() {
    return s_open;
}

void SettingsPage::applyFromUsb(uint8_t brightness, uint8_t contrastPct, uint32_t sleepTimeoutS,
                                uint16_t rotation) {
    // Validate and clamp
    if (brightness < 10 || brightness > 100)
        brightness = DEFAULT_BRIGHTNESS;
    if (contrastPct > 100)
        contrastPct = DEFAULT_CONTRAST;

    s_brightness = brightness;
    s_contrast = contrastPct;
    s_sleepTimeoutS = sleepTimeoutS;
    s_rotation = rotation;

    // Apply immediately
    applyBrightness();

    // Sync UI if panel has been built
    if (s_brSlider)
        lv_slider_set_value(s_brSlider, s_brightness, LV_ANIM_OFF);
    if (s_ctSlider)
        lv_slider_set_value(s_ctSlider, s_contrast, LV_ANIM_OFF);
    updateBrValue();
    updateCtValue();
    updateSleepButtons();
    updateRotButtons();

    // Persist to NVS
    nvsSave();

    LOG_INFO("Settings", "Applied from USB — brightness=%d%% contrast=%d%%", s_brightness,
             s_contrast);
}
