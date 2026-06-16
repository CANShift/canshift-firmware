#include "settings_page.h"
#include "settings_page_internal.h"

#include "app_config.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <stdint.h>

using namespace SettingsPageInternal;

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
    buildDayModeRow(y, rowW);
    y += GAP_ROW;
    buildCalibrateTouchRow(y, rowW);
    y += GAP_INNER;
    buildResetTouchCalRow(y, rowW);
    y += GAP_ROW;
    buildRebootRow(y, rowW);
    y += GAP_ROW;
    buildAboutRow(y, rowW);
    y += GAP_ROW;
    buildActionsRow(y, rowW);

    applyBrightness();
    LOG_INFO("Settings", "Settings page initialized");
}

void SettingsPage::open() {
    if (!s_panel || s_open)
        return;
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

    lv_obj_set_y(s_panel, y);
    if (lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
    }
}

void SettingsPage::snapOpen() {
    if (!s_panel)
        return;
    if (lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN)) {

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
        return;
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
