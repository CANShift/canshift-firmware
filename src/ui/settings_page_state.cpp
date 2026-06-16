#include "settings_page_internal.h"

#include "settings_page.h"
#include "app_config.h"
#include "hal/ble/ble_server.h"
#include "hal/display/display_driver.h"
#include "hal/touch/touch_driver.h"
#include "ui/theme_manager.h"
#include "diag/logger.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>

namespace SettingsPageInternal {

static constexpr char NVS_NS[] = "screen_cfg";
static constexpr char KEY_BRIGHTNESS[] = "brightness";
static constexpr char KEY_BLE_ENABLED[] = "ble_en";

static constexpr bool DEFAULT_BLE_ENABLED = (BLE_DEFAULT_ENABLED != 0);

uint8_t s_brightness = DEFAULT_BRIGHTNESS;
bool s_bleEnabled = DEFAULT_BLE_ENABLED;

lv_obj_t *s_panel = nullptr;
lv_obj_t *s_brSlider = nullptr;
lv_obj_t *s_brValue = nullptr;
lv_obj_t *s_bleBtns[2] = {};
lv_obj_t *s_dayBtns[2] = {};
lv_obj_t *s_resetTouchCalBtn = nullptr;
lv_obj_t *s_resetTouchCalLabel = nullptr;
lv_timer_t *s_resetTouchCalTimer = nullptr;

bool s_open = false;
bool s_dragging = false;
uint32_t s_lastOpenMs = 0;

int16_t s_openY = 0;
int16_t s_closedY = 0;
int16_t s_panelHeight = 0;

void nvsLoad() {
    Preferences p;
    p.begin(NVS_NS, true);
    s_brightness = p.getUChar(KEY_BRIGHTNESS, DEFAULT_BRIGHTNESS);
    s_bleEnabled = p.getUChar(KEY_BLE_ENABLED, DEFAULT_BLE_ENABLED ? 1 : 0) != 0;
    p.end();

    if (s_brightness < 10 || s_brightness > 100)
        s_brightness = DEFAULT_BRIGHTNESS;
}

void nvsSave() {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putUChar(KEY_BRIGHTNESS, s_brightness);
    p.putUChar(KEY_BLE_ENABLED, s_bleEnabled ? 1 : 0);
    p.end();
    LOG_INFO("Settings", "Saved — brightness=%d%% ble=%d", s_brightness, s_bleEnabled ? 1 : 0);
}

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
    const bool active[2] = {s_bleEnabled, !s_bleEnabled};
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

void updateDayModeButtons() {
    const bool dayOn = ThemeManager::isDayMode();
    const bool active[2] = {dayOn, !dayOn};
    for (uint8_t i = 0; i < 2; ++i) {
        if (!s_dayBtns[i])
            continue;
        lv_obj_set_style_bg_color(s_dayBtns[i], lv_color_hex(active[i] ? CLR_BTN_ACT : CLR_BTN_BG),
                                  LV_PART_MAIN);
        lv_obj_set_style_border_color(
            s_dayBtns[i], lv_color_hex(active[i] ? CLR_ACCENT : CLR_BTN_BDR), LV_PART_MAIN);
        lv_obj_t *lbl = lv_obj_get_child(s_dayBtns[i], 0);
        if (lbl)
            lv_obj_set_style_text_color(lbl, lv_color_hex(active[i] ? CLR_ACCENT : CLR_MUTED), 0);
    }
}

void onBrightnessChanged(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    s_brightness = static_cast<uint8_t>(lv_slider_get_value(slider));
    updateBrValue();
    applyBrightness();
}

void onBleBtn(lv_event_t *e) {
    uint32_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    const bool wantEnabled = (idx == 0);
    if (wantEnabled == s_bleEnabled)
        return;
    s_bleEnabled = wantEnabled;
    updateBleButtons();
    nvsSave();
#if APP_BLE_ENABLED
    BleServer::setPendingEnabled(s_bleEnabled);
#endif
    LOG_INFO("Settings", "BLE %s — reboot to apply", s_bleEnabled ? "enabled" : "disabled");
}

void onCalibrateTouch(lv_event_t *) {

    SettingsPage::close();
    TouchDriver::calibrate();
}

static void clearTouchCalFeedback(lv_timer_t *t) {
    if (s_resetTouchCalBtn) {
        lv_obj_t *lbl = lv_obj_get_child(s_resetTouchCalBtn, 0);
        if (lbl) {
            lv_label_set_text(lbl, "RESET TOUCH CAL");
            lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
        }
        lv_obj_set_style_border_color(s_resetTouchCalBtn, lv_color_hex(CLR_BTN_BDR), LV_PART_MAIN);
    }
    lv_timer_del(t);
    s_resetTouchCalTimer = nullptr;
}

void onResetTouchCal(lv_event_t *) {
    TouchDriver::resetCalibration();
    LOG_INFO("Settings", "Touch calibration reset — reboot to apply defaults");

    if (s_resetTouchCalBtn) {
        lv_obj_t *lbl = lv_obj_get_child(s_resetTouchCalBtn, 0);
        if (lbl) {
            lv_label_set_text(lbl, "RESET PENDING - REBOOT");
            lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_ACCENT), 0);
        }
        lv_obj_set_style_border_color(s_resetTouchCalBtn, lv_color_hex(CLR_ACCENT), LV_PART_MAIN);
    }

    if (s_resetTouchCalTimer) {
        lv_timer_del(s_resetTouchCalTimer);
        s_resetTouchCalTimer = nullptr;
    }
    s_resetTouchCalTimer = lv_timer_create(clearTouchCalFeedback, RESET_FEEDBACK_MS, nullptr);
    lv_timer_set_repeat_count(s_resetTouchCalTimer, 1);
}

void onDayModeBtn(lv_event_t *e) {
    const uint32_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    ThemeManager::setDayMode(idx == 0);
    updateDayModeButtons();
}

static lv_timer_t *s_rebootCountdownTimer = nullptr;
static lv_obj_t *s_rebootBtn = nullptr;
static uint32_t s_rebootPressStartMs = 0;

static void writeRebootLabel(const char *text, uint32_t color) {
    if (!s_rebootBtn)
        return;
    lv_obj_t *lbl = lv_obj_get_child(s_rebootBtn, 0);
    if (lbl) {
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    }
}

static void resetRebootBtn() {
    writeRebootLabel("HOLD 3 s TO REBOOT", CLR_MUTED);
    if (s_rebootBtn)
        lv_obj_set_style_border_color(s_rebootBtn, lv_color_hex(CLR_BTN_BDR), LV_PART_MAIN);
}

static void tickRebootCountdown(lv_timer_t *t) {
    const uint32_t elapsed = millis() - s_rebootPressStartMs;
    if (elapsed >= REBOOT_LONG_PRESS_MS) {
        LOG_INFO("Settings", "Reboot requested via settings page long-press");
        lv_timer_del(t);
        s_rebootCountdownTimer = nullptr;
        delay(50);
        esp_restart();
        return;
    }
    const uint32_t remainingMs = REBOOT_LONG_PRESS_MS - elapsed;
    const uint32_t remainingS = (remainingMs + 999) / 1000;
    char buf[24];
    snprintf(buf, sizeof(buf), "REBOOT IN %us", static_cast<unsigned>(remainingS));
    writeRebootLabel(buf, CLR_ACCENT);
}

void onRebootLongPress(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        s_rebootBtn = btn;
        s_rebootPressStartMs = millis();
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_ACCENT), LV_PART_MAIN);
        writeRebootLabel("REBOOT IN 3s", CLR_ACCENT);
        if (s_rebootCountdownTimer) {
            lv_timer_del(s_rebootCountdownTimer);
            s_rebootCountdownTimer = nullptr;
        }
        s_rebootCountdownTimer = lv_timer_create(tickRebootCountdown, 200, nullptr);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_rebootCountdownTimer) {
            lv_timer_del(s_rebootCountdownTimer);
            s_rebootCountdownTimer = nullptr;
        }
        resetRebootBtn();
    }
}

void onSave(lv_event_t *) {
    LOG_INFO("Settings", "SAVE button clicked");
    nvsSave();
    SettingsPage::close();
}

void onReset(lv_event_t *) {
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
