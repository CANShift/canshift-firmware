// settings_page.cpp — On-device LVGL screen settings page

#include "settings_page.h"
#include "ui/font_manager.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
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
static constexpr char KEY_SLEEP_S[] = "sleep_s";       // uint32 (0/30/60/300)

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

static constexpr uint8_t DEFAULT_BRIGHTNESS = 80;
static constexpr uint32_t DEFAULT_SLEEP_S = 0;

// ---------------------------------------------------------------------------
// Sleep timeout options
// ---------------------------------------------------------------------------

static constexpr uint8_t SLEEP_OPTION_COUNT = 4;
static constexpr uint32_t SLEEP_OPTIONS[SLEEP_OPTION_COUNT] = {0, 30, 60, 300};
static const char *const SLEEP_LABELS[SLEEP_OPTION_COUNT] = {"Off", "30s", "1m", "5m"};

// ---------------------------------------------------------------------------
// Colors
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

static uint8_t s_brightness = DEFAULT_BRIGHTNESS;
static uint32_t s_sleepTimeoutS = DEFAULT_SLEEP_S;
static bool s_sleeping = false;

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_brSlider = nullptr;
static lv_obj_t *s_brValue = nullptr;
static lv_obj_t *s_sleepBtns[SLEEP_OPTION_COUNT] = {};

static bool s_open = false;

// -----------------------------------------------------------------------
// NVS helpers
// -----------------------------------------------------------------------

void nvsLoad() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    s_brightness = p.getUChar(KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    s_sleepTimeoutS = p.getUInt(KEY_SLEEP_S, DEFAULT_SLEEP_S);
    p.end();

    if (s_brightness < 10 || s_brightness > 100)
        s_brightness = DEFAULT_BRIGHTNESS;
}

void nvsSave() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putUChar(KEY_BRIGHTNESS, s_brightness);
    p.putUInt(KEY_SLEEP_S, s_sleepTimeoutS);
    p.end();
    LOG_INFO("Settings", "Saved — brightness=%d%% sleep=%ds", s_brightness, s_sleepTimeoutS);
}

// -----------------------------------------------------------------------
// Apply helpers
// -----------------------------------------------------------------------

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
        if (lbl)
            lv_obj_set_style_text_color(lbl, lv_color_hex(active ? CLR_ACCENT : CLR_MUTED), 0);
    }
}

// -----------------------------------------------------------------------
// Event callbacks
// -----------------------------------------------------------------------

static void onBrightnessChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    s_brightness = static_cast<uint8_t>(lv_slider_get_value(slider));
    updateBrValue();
    applyBrightness();
}

static void onSleepBtn(lv_event_t *e) {
    uint32_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (idx >= SLEEP_OPTION_COUNT)
        return;
    s_sleepTimeoutS = SLEEP_OPTIONS[idx];
    s_sleeping = false;
    updateSleepButtons();
}

static void onCalibrateTouch(lv_event_t * /*e*/) {
    // Close settings so calibration crosshairs are unobstructed
    SettingsPage::close();
    TouchDriver::calibrate();
}

static void onSave(lv_event_t * /*e*/) {
    LOG_INFO("Settings", "SAVE button clicked");
    nvsSave();
    SettingsPage::close(); // close after saving — matches user expectation
}

static void onReset(lv_event_t * /*e*/) {
    s_brightness = DEFAULT_BRIGHTNESS;
    s_sleepTimeoutS = DEFAULT_SLEEP_S;
    s_sleeping = false;

    lv_slider_set_value(s_brSlider, s_brightness, LV_ANIM_OFF);
    updateBrValue();
    updateSleepButtons();
    applyBrightness();
}

// -----------------------------------------------------------------------
// Layout helpers
// -----------------------------------------------------------------------

// Lazy accessors — file-scope static initializers run before FontManager::init()
// in boot_sequence, so caching the pointer at static-init time would freeze it
// to LV_FONT_DEFAULT instead of the actual size 12 loaded from SPIFFS.
static inline const lv_font_t *FONT_LG() { return FontManager::get(12); }
static inline const lv_font_t *FONT_SM() { return FontManager::get(12); }

static constexpr int16_t PAD_H = 8;

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
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
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
        lv_obj_set_style_text_font(title, FONT_LG(), 0);
        lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), 0);
        lv_obj_set_pos(title, PAD_H, y);
        y += 18;
    }

    // ---- Brightness ----
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
        lv_label_set_text(lbl, "BRIGHTNESS");
        lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        s_brValue = lv_label_create(row);
        lv_obj_set_style_text_font(s_brValue, FONT_SM(), 0);
        lv_obj_set_style_text_color(s_brValue, lv_color_hex(CLR_TEXT), 0);
        lv_obj_align(s_brValue, LV_ALIGN_RIGHT_MID, 0, 0);
        updateBrValue();

        y += labelH + gapInner;

        s_brSlider = makeSlider(s_panel, 10, 100, s_brightness, onBrightnessChanged);
        lv_obj_set_pos(s_brSlider, PAD_H, y);
        lv_obj_set_size(s_brSlider, rowW, sliderH);
        y += sliderH;
    }

    // ---- Sleep timeout ----
    y += gapRow;
    {
        lv_obj_t *lbl = lv_label_create(s_panel);
        lv_label_set_text(lbl, "SLEEP");
        lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
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

    // ---- Touch calibration ----
    y += gapRow;
    {
        lv_obj_t *calBtn = lv_btn_create(s_panel);
        lv_obj_set_pos(calBtn, PAD_H, y);
        lv_obj_set_size(calBtn, rowW, btnH);
        lv_obj_set_style_bg_color(calBtn, lv_color_hex(CLR_BTN_BG), LV_PART_MAIN);
        lv_obj_set_style_border_color(calBtn, lv_color_hex(CLR_BTN_BDR), LV_PART_MAIN);
        lv_obj_set_style_border_width(calBtn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(calBtn, 3, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(calBtn, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(calBtn, 3, LV_PART_MAIN);
        lv_obj_t *calLbl = lv_label_create(calBtn);
        lv_label_set_text(calLbl, "CALIBRATE TOUCH");
        lv_obj_set_style_text_font(calLbl, FONT_SM(), 0);
        lv_obj_set_style_text_color(calLbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_center(calLbl);
        lv_obj_add_event_cb(calBtn, onCalibrateTouch, LV_EVENT_CLICKED, nullptr);
        y += btnH;
    }

    // ---- Actions — pinned to bottom ----
    {
        const int16_t actionY = height - 8 - btnH;
        const int16_t gap = 6;
        const int16_t resetW = (rowW - gap) / 3;
        const int16_t saveW = rowW - resetW - gap;

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
        lv_obj_set_style_text_font(resetLbl, FONT_SM(), 0);
        lv_obj_set_style_text_color(resetLbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_center(resetLbl);
        lv_obj_add_event_cb(resetBtn, onReset, LV_EVENT_CLICKED, nullptr);

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
        lv_obj_set_style_text_font(saveLbl, FONT_SM(), 0);
        lv_obj_set_style_text_color(saveLbl, lv_color_hex(CLR_SAVE_TEXT), 0);
        lv_obj_center(saveLbl);
        lv_obj_add_event_cb(saveBtn, onSave, LV_EVENT_CLICKED, nullptr);
    }

    applyBrightness();
    LOG_INFO("Settings", "Settings page initialized");
}

static uint32_t s_lastOpenMs = 0;

void SettingsPage::open() {
    if (!s_panel || s_open)
        return;
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
    s_open = true;
    s_lastOpenMs = millis();
    LOG_DEBUG("Settings", "Settings page opened");
}

uint32_t SettingsPage::lastOpenMs() {
    return s_lastOpenMs;
}

void SettingsPage::close() {
    if (!s_panel || !s_open)
        return;
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
    LOG_DEBUG("Settings", "Settings page closed");
}

bool SettingsPage::toggle() {
    if (s_open)
        close();
    else
        open();
    return s_open;
}

bool SettingsPage::isOpen() {
    return s_open;
}

uint32_t SettingsPage::getSleepTimeoutS() {
    return s_sleepTimeoutS;
}

uint8_t SettingsPage::getBrightness() {
    return s_brightness;
}

void SettingsPage::tickSleep() {
    if (s_sleepTimeoutS == 0)
        return;

    uint32_t inactiveMs = lv_disp_get_inactive_time(nullptr);
    bool shouldSleep = (inactiveMs >= s_sleepTimeoutS * 1000u);

    if (shouldSleep && !s_sleeping) {
        s_sleeping = true;
        DisplayDriver::setBacklight(0);
    } else if (!shouldSleep && s_sleeping) {
        s_sleeping = false;
        applyBrightness();
    }
}

void SettingsPage::applyFromUsb(uint8_t brightness, uint32_t sleepTimeoutS) {
    if (brightness < 10 || brightness > 100)
        brightness = DEFAULT_BRIGHTNESS;

    s_brightness = brightness;
    s_sleepTimeoutS = sleepTimeoutS;
    s_sleeping = false;

    applyBrightness();

    if (s_brSlider)
        lv_slider_set_value(s_brSlider, s_brightness, LV_ANIM_OFF);
    updateBrValue();
    updateSleepButtons();

    nvsSave();

    LOG_INFO("Settings", "Applied from USB — brightness=%d%% sleep=%ds", s_brightness,
             s_sleepTimeoutS);
}
