#pragma once

#include "app_config.h"

#include <lvgl.h>
#include <stdint.h>

namespace SettingsPageInternal {

static constexpr uint32_t kClrBg = 0x0D0D0D;
static constexpr uint32_t kClrAccent = 0xFF4747;
static constexpr uint32_t kClrText = 0xCCCCCC;
static constexpr uint32_t kClrMuted = 0x555555;
static constexpr uint32_t kClrBtnBg = 0x111111;
static constexpr uint32_t kClrBtnAct = 0x1A0A0A;
static constexpr uint32_t kClrBtnBdr = 0x2A2A2A;
static constexpr uint32_t kClrSaveBg = 0x1A3A1A;
static constexpr uint32_t kClrSaveBdr = 0x336633;
static constexpr uint32_t kClrSaveText = 0x55AA55;

static constexpr int16_t kPadH = 8;
static constexpr int16_t kSliderH = 16;
static constexpr int16_t kLabelH = 14;

// DASH_DESIGN_SYSTEM.md §6 puts the floor for a button at 48; the panel
// fits one screen at that height and scrolls at anything taller.
static constexpr int16_t kBtnH = 48;
static constexpr int16_t kGapRow = 10;
static constexpr int16_t kGapInner = 6;

static constexpr uint32_t kSnapAnimMs = 180;

static constexpr uint8_t kDefaultBrightness = 80;

static constexpr uint32_t kRebootLongPressMs = 3000;
static constexpr uint32_t kResetFeedbackMs = 3000;

extern uint8_t s_brightness;

extern lv_obj_t *s_panel;
extern lv_obj_t *s_brSlider;
extern lv_obj_t *s_brValue;
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

void onBrightnessChanged(lv_event_t *e);
void onCalibrateTouch(lv_event_t *e);
void onResetTouchCal(lv_event_t *e);
void onDayModeBtn(lv_event_t *e);
void onRebootLongPress(lv_event_t *e);
void onSave(lv_event_t *e);
void onReset(lv_event_t *e);

void updateDayModeButtons();

lv_obj_t *makeFullButton(lv_obj_t *parent, const char *label, uint32_t bgColor, uint32_t bdrColor,
                         uint32_t textColor, lv_event_cb_t cb);

void computePanelGeometry(int16_t yOffset, int16_t height);
lv_obj_t *createPanel(int16_t yOffset, int16_t panelW, int16_t height);

void buildBrightnessRow(int16_t &y, int16_t rowW);

void buildCalibrateTouchRow(int16_t &y, int16_t rowW);
void buildResetTouchCalRow(int16_t &y, int16_t rowW);
void buildDayModeRow(int16_t &y, int16_t rowW);
void buildAboutRow(int16_t &y, int16_t rowW);
void buildRebootRow(int16_t &y, int16_t rowW);
#if APP_WORKSHOP_ENABLED
void buildWorkshopRow(int16_t &y, int16_t rowW);
void refreshWorkshopRow();
#else
inline void buildWorkshopRow(int16_t &, int16_t) {}
inline void refreshWorkshopRow() {}
#endif

void runSnap(int16_t targetY, lv_anim_ready_cb_t doneCb);
void onSnapOpenDone(lv_anim_t *a);
void onSnapClosedDone(lv_anim_t *a);
void animSetY(void *obj, int32_t v);

}
