// settings_page.cpp — On-device LVGL screen settings page

#include "settings_page.h"
#include "app_config.h"
#include "ui/font_manager.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "hal/ble/ble_server.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <Preferences.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// NVS namespace and key names
// ---------------------------------------------------------------------------

static constexpr char NVS_NS[] = "screen_cfg";
static constexpr char KEY_BRIGHTNESS[] = "brightness"; // uint8  (10–100 %)
static constexpr char KEY_BLE_ENABLED[] = "ble_en";    // uint8  (0/1)

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

static constexpr uint8_t DEFAULT_BRIGHTNESS = 80;
// Tracks `BLE_DEFAULT_ENABLED` in app_config.h — keep both in sync so the
// Settings page reset button and the NVS-load fallback agree (issue #878).
static constexpr bool DEFAULT_BLE_ENABLED = (BLE_DEFAULT_ENABLED != 0);

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
static bool s_bleEnabled = DEFAULT_BLE_ENABLED;

static lv_obj_t *s_panel = nullptr;
static lv_obj_t *s_brSlider = nullptr;
static lv_obj_t *s_brValue = nullptr;
static lv_obj_t *s_bleBtns[2] = {};

static bool s_open = false;
static bool s_dragging = false;

// Resolved during init() — the y the panel sits at when visible, and the y
// it sits at when fully tucked off-screen (just above the top bar).
static int16_t s_openY = 0;
static int16_t s_closedY = 0;
static int16_t s_panelHeight = 0;

// Snap animation duration — short enough to feel responsive, long enough to
// read as motion rather than a teleport.
static constexpr uint32_t SNAP_ANIM_MS = 180;

// -----------------------------------------------------------------------
// Layout constants — shared across init() helpers
// -----------------------------------------------------------------------

static constexpr int16_t PAD_H = 8;
static constexpr int16_t SLIDER_H = 16;
static constexpr int16_t LABEL_H = 14;
// 56-px touch targets — comfortably above Apple HIG / Material 44 px guidance.
// The page now scrolls vertically, so we are no longer constrained by the
// 240 px screen height.
static constexpr int16_t BTN_H = 56;
static constexpr int16_t GAP_ROW = 10;
static constexpr int16_t GAP_INNER = 6;
static constexpr int16_t HEADER_H = 18;

// -----------------------------------------------------------------------
// NVS helpers
// -----------------------------------------------------------------------

void nvsLoad() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/true);
    s_brightness = p.getUChar(KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    s_bleEnabled = p.getUChar(KEY_BLE_ENABLED, DEFAULT_BLE_ENABLED ? 1 : 0) != 0;
    p.end();

    if (s_brightness < 10 || s_brightness > 100)
        s_brightness = DEFAULT_BRIGHTNESS;
}

void nvsSave() {
    Preferences p;
    p.begin(NVS_NS, /*readOnly=*/false);
    p.putUChar(KEY_BRIGHTNESS, s_brightness);
    p.putUChar(KEY_BLE_ENABLED, s_bleEnabled ? 1 : 0);
    p.end();
    LOG_INFO("Settings", "Saved — brightness=%d%% ble=%d", s_brightness, s_bleEnabled ? 1 : 0);
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

void updateBleButtons() {
    const bool active[2] = {s_bleEnabled, !s_bleEnabled}; // ON=idx0, OFF=idx1
    for (uint8_t i = 0; i < 2; ++i) {
        if (!s_bleBtns[i])
            continue;
        lv_obj_set_style_bg_color(s_bleBtns[i], lv_color_hex(active[i] ? CLR_BTN_ACT : CLR_BTN_BG),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_color(
            s_bleBtns[i], lv_color_hex(active[i] ? CLR_ACCENT : CLR_BTN_BDR), LV_PART_MAIN);
        lv_obj_t *lbl = lv_obj_get_child(s_bleBtns[i], 0);
        if (lbl)
            lv_obj_set_style_text_color(lbl, lv_color_hex(active[i] ? CLR_ACCENT : CLR_MUTED), 0);
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

static void onBleBtn(lv_event_t *e) {
    uint32_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    // idx 0 = ON, idx 1 = OFF
    s_bleEnabled = (idx == 0);
    updateBleButtons();
#if APP_BLE_ENABLED
    BleServer::setPendingEnabled(s_bleEnabled);
#endif
}

static void onCalibrateTouch(lv_event_t * /*e*/) {
    // Close settings so calibration crosshairs are unobstructed
    SettingsPage::close();
    TouchDriver::calibrate();
}

static void onResetTouchCal(lv_event_t * /*e*/) {
    // Wipes the saved offsets — next boot falls back to board defaults and
    // re-runs first-boot calibration. The current session keeps the cached
    // offsets so the user can still navigate the UI to reboot.
    TouchDriver::resetCalibration();
    LOG_INFO("Settings", "Touch calibration reset — reboot to apply defaults");
}

static void onSave(lv_event_t * /*e*/) {
    LOG_INFO("Settings", "SAVE button clicked");
    nvsSave();
    SettingsPage::close(); // close after saving — matches user expectation
}

static void onReset(lv_event_t * /*e*/) {
    s_brightness = DEFAULT_BRIGHTNESS;
    s_bleEnabled = DEFAULT_BLE_ENABLED;

    lv_slider_set_value(s_brSlider, s_brightness, LV_ANIM_OFF);
    updateBrValue();
    updateBleButtons();
    applyBrightness();
#if APP_BLE_ENABLED
    BleServer::setPendingEnabled(s_bleEnabled);
#endif
}

// -----------------------------------------------------------------------
// Layout helpers
// -----------------------------------------------------------------------

// Lazy accessors — file-scope static initializers run before FontManager::init()
// in boot_sequence, so caching the pointer at static-init time would freeze it
// to LV_FONT_DEFAULT instead of the actual size 12 loaded from SPIFFS.
static inline const lv_font_t *FONT_LG() {
    return FontManager::label(12);
}
static inline const lv_font_t *FONT_SM() {
    return FontManager::label(12);
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
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(active ? CLR_ACCENT : CLR_MUTED), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
    return btn;
}

// Full-width labeled button used for calibrate / reset-touch-cal / reset / save.
// All four share identical box styling — only colors and the label string differ.
lv_obj_t *makeFullButton(lv_obj_t *parent, const char *label, uint32_t bgColor, uint32_t bdrColor,
                         uint32_t textColor, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(bdrColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 3, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(textColor), 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

// -----------------------------------------------------------------------
// Init phase helpers — each builds one logical section of the page.
// Helpers that flow content vertically take `y` by reference and advance it.
// -----------------------------------------------------------------------

void computePanelGeometry(int16_t yOffset, int16_t height) {
    // Panel geometry: visible at yOffset (just below the top bar); hidden by
    // translating up by its own height so it sits flush with the top bar's
    // bottom edge but off-screen. Drag tracker interpolates between the two.
    s_openY = yOffset;
    s_panelHeight = height;
    s_closedY = static_cast<int16_t>(yOffset - height);
}

lv_obj_t *createPanel(int16_t yOffset, int16_t panelW, int16_t height) {
    lv_obj_t *panel = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(panel, 0, yOffset);
    lv_obj_set_size(panel, panelW, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel, 8, LV_PART_MAIN);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

void buildHeader(int16_t &y) {
    lv_obj_t *title = lv_label_create(s_panel);
    lv_label_set_text(title, "SCREEN SETTINGS");
    lv_obj_set_style_text_font(title, FONT_LG(), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_pos(title, PAD_H, y);
    y += HEADER_H;
}

void buildBrightnessRow(int16_t &y, int16_t rowW) {
    lv_obj_t *row = lv_obj_create(s_panel);
    lv_obj_set_pos(row, PAD_H, y);
    lv_obj_set_size(row, rowW, LABEL_H);
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

    y += LABEL_H + GAP_INNER;

    s_brSlider = makeSlider(s_panel, 10, 100, s_brightness, onBrightnessChanged);
    lv_obj_set_pos(s_brSlider, PAD_H, y);
    lv_obj_set_size(s_brSlider, rowW, SLIDER_H);
    y += SLIDER_H;
}

// Labeled "MOBILE PAIRING" so a user lands here understands the toggle
// gates the canshift-mobile companion app, not generic Bluetooth audio
// or arbitrary BLE peripherals (#878).
void buildBleRow(int16_t &y, int16_t rowW) {
    lv_obj_t *lbl = lv_label_create(s_panel);
    lv_label_set_text(lbl, "MOBILE PAIRING");
    lv_obj_set_style_text_font(lbl, FONT_SM(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_pos(lbl, PAD_H, y);
    y += LABEL_H + GAP_INNER;

    const int16_t gap = 4;
    const int16_t btnW = (rowW - gap) / 2;
    const char *const bleLabels[2] = {"ON", "OFF"};
    for (uint8_t i = 0; i < 2; ++i) {
        bool active = (i == 0) ? s_bleEnabled : !s_bleEnabled;
        s_bleBtns[i] = makeSegButton(s_panel, bleLabels[i], active, onBleBtn,
                                     reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_set_pos(s_bleBtns[i], PAD_H + i * (btnW + gap), y);
        lv_obj_set_size(s_bleBtns[i], btnW, BTN_H);
    }
    y += BTN_H;
}

void buildCalibrateTouchRow(int16_t &y, int16_t rowW) {
    lv_obj_t *btn = makeFullButton(s_panel, "CALIBRATE TOUCH", CLR_BTN_BG, CLR_BTN_BDR, CLR_MUTED,
                                   onCalibrateTouch);
    lv_obj_set_pos(btn, PAD_H, y);
    lv_obj_set_size(btn, rowW, BTN_H);
    y += BTN_H;
}

void buildResetTouchCalRow(int16_t &y, int16_t rowW) {
    lv_obj_t *btn = makeFullButton(s_panel, "RESET TOUCH CAL", CLR_BTN_BG, CLR_BTN_BDR, CLR_MUTED,
                                   onResetTouchCal);
    lv_obj_set_pos(btn, PAD_H, y);
    lv_obj_set_size(btn, rowW, BTN_H);
    y += BTN_H;
}

// Actions row — Reset (1/3) + Save (2/3) laid out side-by-side at the
// end of the scrollable content.
void buildActionsRow(int16_t y, int16_t rowW) {
    const int16_t gap = 6;
    const int16_t resetW = (rowW - gap) / 3;
    const int16_t saveW = rowW - resetW - gap;

    lv_obj_t *resetBtn =
        makeFullButton(s_panel, "RESET", CLR_BTN_BG, CLR_BTN_BDR, CLR_MUTED, onReset);
    lv_obj_set_pos(resetBtn, PAD_H, y);
    lv_obj_set_size(resetBtn, resetW, BTN_H);

    lv_obj_t *saveBtn =
        makeFullButton(s_panel, "SAVE", CLR_SAVE_BG, CLR_SAVE_BDR, CLR_SAVE_TEXT, onSave);
    lv_obj_set_pos(saveBtn, PAD_H + resetW + gap, y);
    lv_obj_set_size(saveBtn, saveW, BTN_H);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SettingsPage::init(int16_t yOffset, int16_t height) {
    nvsLoad();

    const int16_t panelW = LV_HOR_RES;
    const int16_t rowW = panelW - PAD_H * 2;

    computePanelGeometry(yOffset, height);
    s_panel = createPanel(yOffset, panelW, height);

    int16_t y = 6;
    buildHeader(y);
    y += GAP_ROW;
    buildBrightnessRow(y, rowW);
    y += GAP_ROW;
    buildBleRow(y, rowW);
    y += GAP_ROW;
    buildCalibrateTouchRow(y, rowW);
    y += GAP_INNER;
    buildResetTouchCalRow(y, rowW);
    y += GAP_ROW;
    buildActionsRow(y, rowW);

    applyBrightness();
    LOG_INFO("Settings", "Settings page initialized");
}

static uint32_t s_lastOpenMs = 0;

void SettingsPage::open() {
    if (!s_panel || s_open)
        return;
    // Snap to the resting open position in case a previous drag/snap left the
    // panel at an interpolated y.
    lv_obj_set_y(s_panel, s_openY);
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
    // Reset position so the next open()/snapOpen() starts from a known state.
    lv_obj_set_y(s_panel, s_openY);
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

uint8_t SettingsPage::getBrightness() {
    return s_brightness;
}

bool SettingsPage::getBleEnabled() {
    return s_bleEnabled;
}

// ---------------------------------------------------------------------------
// Drag-to-reveal (issue #47)
// ---------------------------------------------------------------------------

int16_t SettingsPage::getOpenY() {
    return s_openY;
}

int16_t SettingsPage::getClosedY() {
    return s_closedY;
}

int16_t SettingsPage::getPanelHeight() {
    return s_panelHeight;
}

bool SettingsPage::isDragging() {
    return s_dragging;
}

void SettingsPage::setDragging(bool dragging) {
    s_dragging = dragging;
}

void SettingsPage::setPanelY(int16_t y) {
    if (!s_panel)
        return;
    if (y < s_closedY)
        y = s_closedY;
    if (y > s_openY)
        y = s_openY;
    // Position before reveal — otherwise the panel flashes for one frame at
    // its previous resting y before our drag offset takes effect.
    lv_obj_set_y(s_panel, y);
    if (lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
    }
}

namespace {

// Animation hop — written for both open and close because the executor needs
// a plain `void(void*, int32_t)` signature and we want to commit the final
// state once the animation finishes.

void animSetY(void *obj, int32_t v) {
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
}

void onSnapOpenDone(lv_anim_t * /*a*/) {
    if (!s_panel)
        return;
    lv_obj_set_y(s_panel, s_openY);
    if (!s_open) {
        s_open = true;
        s_lastOpenMs = millis();
        LOG_DEBUG("Settings", "Settings page opened (snap)");
    }
}

void onSnapClosedDone(lv_anim_t * /*a*/) {
    if (!s_panel)
        return;
    lv_obj_set_y(s_panel, s_closedY);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_open) {
        s_open = false;
        LOG_DEBUG("Settings", "Settings page closed (snap)");
    }
}

void runSnap(int16_t targetY, lv_anim_ready_cb_t doneCb) {
    if (!s_panel)
        return;
    int16_t fromY = lv_obj_get_y(s_panel);
    // Cancel any in-flight snap on this object so a fast user motion doesn't
    // stack two animations fighting for the same y.
    lv_anim_del(s_panel, animSetY);

    // Distance-proportional duration — short hops feel snappier without
    // capping how much motion the user can see in one gesture.
    uint32_t deltaPx = static_cast<uint32_t>(abs(targetY - fromY));
    uint32_t panelH = static_cast<uint32_t>(s_panelHeight > 0 ? s_panelHeight : 1);
    uint32_t durationMs = (SNAP_ANIM_MS * deltaPx) / panelH;
    if (durationMs < 60)
        durationMs = 60;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, animSetY);
    lv_anim_set_values(&a, fromY, targetY);
    lv_anim_set_time(&a, durationMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, doneCb);
    lv_anim_start(&a);
}

} // namespace

void SettingsPage::snapOpen() {
    if (!s_panel)
        return;
    if (lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN)) {
        // Coming from a fully-closed state with no drag preview — start the
        // animation from s_closedY rather than the resting open position.
        lv_obj_set_y(s_panel, s_closedY);
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
    }
    runSnap(s_openY, onSnapOpenDone);
}

void SettingsPage::snapClosed() {
    if (!s_panel)
        return;
    if (lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))
        return; // Already hidden, nothing to animate.
    runSnap(s_closedY, onSnapClosedDone);
}

void SettingsPage::applyFromUsb(uint8_t brightness) {
    if (brightness < 10 || brightness > 100)
        brightness = DEFAULT_BRIGHTNESS;

    s_brightness = brightness;
    applyBrightness();

    if (s_brSlider)
        lv_slider_set_value(s_brSlider, s_brightness, LV_ANIM_OFF);
    updateBrValue();
    nvsSave();

    LOG_INFO("Settings", "Applied from USB — brightness=%d%%", s_brightness);
}
