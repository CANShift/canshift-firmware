#pragma once
// settings_page_internal.h — Cross-module forward decls for the on-device
// LVGL settings page. Carved out of settings_page.cpp during the #1207
// refactor. Three translation units share this header:
//
//   - settings_page.cpp        — orchestrator + public API (open/close/snap…)
//   - settings_page_ui.cpp     — LVGL widget tree builders, animation hops
//   - settings_page_state.cpp  — NVS persistence + brightness/BLE/WIFI state
//                                machine + LVGL event callbacks
//
// All translation units assume the LVGL mutex (g_lvglMutex) is held when
// they touch LVGL primitives, matching the contract of the public
// SettingsPage API.

#include <lvgl.h>
#include <stdint.h>

namespace SettingsPageInternal {

// ---------------------------------------------------------------------------
// Colors — shared by builders and state→UI sync helpers
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
// Layout constants — shared by builders and the orchestrator's init() flow
// ---------------------------------------------------------------------------

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

// Snap animation duration — short enough to feel responsive, long enough to
// read as motion rather than a teleport.
static constexpr uint32_t SNAP_ANIM_MS = 180;

// ---------------------------------------------------------------------------
// Defaults — exposed so the orchestrator's USB push path can clamp invalid
// values back to the firmware default rather than silently saving garbage.
// ---------------------------------------------------------------------------

static constexpr uint8_t DEFAULT_BRIGHTNESS = 80;

// ---------------------------------------------------------------------------
// Shared mutable state — defined in settings_page_state.cpp
// ---------------------------------------------------------------------------

extern uint8_t s_brightness;
extern bool s_bleEnabled;

// LVGL handles — created by settings_page_ui.cpp during init, mutated by the
// state machine when broadcasting state changes back to the widget tree.
extern lv_obj_t *s_panel;
extern lv_obj_t *s_brSlider;
extern lv_obj_t *s_brValue;
extern lv_obj_t *s_bleBtns[2];

extern bool s_open;
extern bool s_dragging;
extern uint32_t s_lastOpenMs;

extern int16_t s_openY;
extern int16_t s_closedY;
extern int16_t s_panelHeight;

// ---------------------------------------------------------------------------
// State module — persistence + brightness/BLE/WIFI logic
// (settings_page_state.cpp)
// ---------------------------------------------------------------------------

void nvsLoad();
void nvsSave();

void applyBrightness();

// State→UI sync — push the current s_brightness / s_bleEnabled value into the
// corresponding widget(s).
void updateBrValue();
void updateBleButtons();

// LVGL event callbacks. Declared with C-style void* user data so the builder
// can wire them directly into lv_obj_add_event_cb().
void onBrightnessChanged(lv_event_t *e);
void onBleBtn(lv_event_t *e);
void onCalibrateTouch(lv_event_t *e);
void onResetTouchCal(lv_event_t *e);
void onSave(lv_event_t *e);
void onReset(lv_event_t *e);

// ---------------------------------------------------------------------------
// UI module — LVGL widget tree builders + snap animation
// (settings_page_ui.cpp)
// ---------------------------------------------------------------------------

void computePanelGeometry(int16_t yOffset, int16_t height);
lv_obj_t *createPanel(int16_t yOffset, int16_t panelW, int16_t height);

void buildHeader(int16_t &y);
void buildBrightnessRow(int16_t &y, int16_t rowW);
// buildBleRow is no longer wired into init() — the toggle was removed in
// favour of WiFi-implied BLE control — but the builder is kept as a hidden
// surface so the row can be re-enabled without re-deriving the layout.
void buildBleRow(int16_t &y, int16_t rowW);
void buildCalibrateTouchRow(int16_t &y, int16_t rowW);
void buildResetTouchCalRow(int16_t &y, int16_t rowW);
void buildActionsRow(int16_t y, int16_t rowW);

// Snap animation primitives, used by SettingsPage::snapOpen / snapClosed.
void runSnap(int16_t targetY, lv_anim_ready_cb_t doneCb);
void onSnapOpenDone(lv_anim_t *a);
void onSnapClosedDone(lv_anim_t *a);
void animSetY(void *obj, int32_t v);

} // namespace SettingsPageInternal
