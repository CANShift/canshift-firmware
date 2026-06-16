#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace SettingsPageInternal {

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

static constexpr int16_t PAD_H = 8;
static constexpr int16_t SLIDER_H = 16;
static constexpr int16_t LABEL_H = 14;

static constexpr int16_t BTN_H = 56;
static constexpr int16_t GAP_ROW = 10;
static constexpr int16_t GAP_INNER = 6;
static constexpr int16_t HEADER_H = 18;

static constexpr uint32_t SNAP_ANIM_MS = 180;

static constexpr uint8_t DEFAULT_BRIGHTNESS = 80;

static constexpr uint32_t REBOOT_LONG_PRESS_MS = 3000;
static constexpr uint32_t RESET_FEEDBACK_MS = 3000;

extern uint8_t s_brightness;
extern bool s_bleEnabled;

extern lv_obj_t *s_panel;
extern lv_obj_t *s_brSlider;
extern lv_obj_t *s_brValue;
extern lv_obj_t *s_bleBtns[2];
extern lv_obj_t *s_dayBtns[2];
extern lv_obj_t *s_resetTouchCalBtn;
extern lv_obj_t *s_resetTouchCalLabel;
extern lv_timer_t *s_resetTouchCalTimer;

extern bool s_open;
extern bool s_dragging;
extern uint32_t s_lastOpenMs;

extern int16_t s_openY;
extern int16_t s_closedY;
extern int16_t s_panelHeight;

void nvsLoad();
void nvsSave();

void applyBrightness();

void updateBrValue();
void updateBleButtons();

void onBrightnessChanged(lv_event_t *e);
void onBleBtn(lv_event_t *e);
void onCalibrateTouch(lv_event_t *e);
void onResetTouchCal(lv_event_t *e);
void onDayModeBtn(lv_event_t *e);
void onRebootLongPress(lv_event_t *e);
void onSave(lv_event_t *e);
void onReset(lv_event_t *e);

void updateDayModeButtons();

void computePanelGeometry(int16_t yOffset, int16_t height);
lv_obj_t *createPanel(int16_t yOffset, int16_t panelW, int16_t height);

void buildHeader(int16_t &y);
void buildBrightnessRow(int16_t &y, int16_t rowW);

void buildBleRow(int16_t &y, int16_t rowW);
void buildCalibrateTouchRow(int16_t &y, int16_t rowW);
void buildResetTouchCalRow(int16_t &y, int16_t rowW);
void buildDayModeRow(int16_t &y, int16_t rowW);
void buildAboutRow(int16_t &y, int16_t rowW);
void buildRebootRow(int16_t &y, int16_t rowW);
void buildActionsRow(int16_t y, int16_t rowW);

void runSnap(int16_t targetY, lv_anim_ready_cb_t doneCb);
void onSnapOpenDone(lv_anim_t *a);
void onSnapClosedDone(lv_anim_t *a);
void animSetY(void *obj, int32_t v);

} // namespace SettingsPageInternal
