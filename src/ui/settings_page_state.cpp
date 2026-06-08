// settings_page_state.cpp — Persistence + brightness / BLE / WIFI AP state
// machine for the on-device settings page. Split from settings_page.cpp
// during the #1207 refactor.
//
// This translation unit owns the *state* the settings page reads & writes:
//   - NVS load / save under the "screen_cfg" namespace.
//   - LVGL event callbacks (slider drag, segmented buttons, full buttons).
//   - Re-syncing the widget tree when a value changes (updateBr/Ble/WifiAp).
//
// It deliberately does NOT build the widget tree — that lives in
// settings_page_ui.cpp. Widget *pointers* are read from shared storage
// declared in settings_page_internal.h.

#include "settings_page_internal.h"

#include "settings_page.h"
#include "app_config.h"
#include "hal/ble/ble_server.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "diag/logger.h"

#include <Preferences.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>

namespace SettingsPageInternal {

// ---------------------------------------------------------------------------
// NVS namespace and key names
// ---------------------------------------------------------------------------

static constexpr char NVS_NS[] = "screen_cfg";
static constexpr char KEY_BRIGHTNESS[] = "brightness"; // uint8  (10–100 %)
static constexpr char KEY_BLE_ENABLED[] = "ble_en";    // uint8  (0/1)

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

// DEFAULT_BRIGHTNESS is declared in settings_page_internal.h so the
// orchestrator's applyFromUsb() can clamp invalid USB pushes to the same
// fallback used here.
//
// Tracks `BLE_DEFAULT_ENABLED` in app_config.h — keep both in sync so the
// Settings page reset button and the NVS-load fallback agree (issue #878).
static constexpr bool DEFAULT_BLE_ENABLED = (BLE_DEFAULT_ENABLED != 0);

// ---------------------------------------------------------------------------
// Shared mutable state — declared extern in settings_page_internal.h
// ---------------------------------------------------------------------------

uint8_t s_brightness = DEFAULT_BRIGHTNESS;
bool s_bleEnabled = DEFAULT_BLE_ENABLED;

lv_obj_t *s_panel = nullptr;
lv_obj_t *s_brSlider = nullptr;
lv_obj_t *s_brValue = nullptr;
lv_obj_t *s_bleBtns[2] = {};

bool s_open = false;
bool s_dragging = false;
uint32_t s_lastOpenMs = 0;

// Resolved during init() — the y the panel sits at when visible, and the y
// it sits at when fully tucked off-screen (just above the top bar).
int16_t s_openY = 0;
int16_t s_closedY = 0;
int16_t s_panelHeight = 0;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Apply helpers
// ---------------------------------------------------------------------------

static inline uint8_t brightnessToBacklight(uint8_t pct) {
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

// ---------------------------------------------------------------------------
// Event callbacks
// ---------------------------------------------------------------------------

void onBrightnessChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    s_brightness = static_cast<uint8_t>(lv_slider_get_value(slider));
    updateBrValue();
    applyBrightness();
}

void onBleBtn(lv_event_t *e) {
    uint32_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    // idx 0 = ON, idx 1 = OFF
    s_bleEnabled = (idx == 0);
    updateBleButtons();
#if APP_BLE_ENABLED
    BleServer::setPendingEnabled(s_bleEnabled);
#endif
}

void onCalibrateTouch(lv_event_t * /*e*/) {
    // Close settings so calibration crosshairs are unobstructed
    SettingsPage::close();
    TouchDriver::calibrate();
}

void onResetTouchCal(lv_event_t * /*e*/) {
    // Wipes the saved offsets — next boot falls back to board defaults and
    // re-runs first-boot calibration. The current session keeps the cached
    // offsets so the user can still navigate the UI to reboot.
    TouchDriver::resetCalibration();
    LOG_INFO("Settings", "Touch calibration reset — reboot to apply defaults");
}

void onSave(lv_event_t * /*e*/) {
    LOG_INFO("Settings", "SAVE button clicked");
    nvsSave();
    SettingsPage::close(); // close after saving — matches user expectation
}

void onReset(lv_event_t * /*e*/) {
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

} // namespace SettingsPageInternal
