#pragma once
// alert_flash.h — Per-widget red flash overlay + label re-tinting (issue #133).
//
// Pulses a translucent red rectangle on top of the widget at ~1 Hz, and flips a
// short list of labels to white while the alert is active. Reverts cleanly when
// the value falls back below the threshold.

#include <lvgl.h>
#include <stdint.h>

namespace AlertFlash {

constexpr uint8_t MAX_TRACKED_LABELS = 4;

struct State {
    lv_obj_t *overlay = nullptr; // Red rectangle, full container size, bg_opa pulses 0..180
    bool active = false;
    lv_obj_t *labels[MAX_TRACKED_LABELS] = {};
    uint32_t restoreRgb[MAX_TRACKED_LABELS] = {};
    uint8_t labelCount = 0;
};

/**
 * Attach a red flash overlay to `container`. Call once at widget create time,
 * after the rest of the widget hierarchy has been laid out so the overlay sits
 * on top. The overlay is fully transparent until update() flips it on.
 */
void attach(State &s, lv_obj_t *container);

/**
 * Register a label whose colour should turn white while the alert is active.
 * `restoreRgb` is the colour to restore when the alert clears.
 * Up to MAX_TRACKED_LABELS labels per widget.
 */
void watchLabel(State &s, lv_obj_t *label, uint32_t restoreRgb);

/**
 * Drive the alert state from the latest signal value.
 *   - NaN threshold or value <= threshold → ensure alert is off
 *   - value > threshold                   → ensure alert is on
 * Idempotent: only triggers anim/style changes on transitions.
 */
void update(State &s, float value, float threshold);

} // namespace AlertFlash
