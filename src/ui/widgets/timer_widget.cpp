// timer_widget.cpp — Lap/session timer widget
//
// Tracks elapsed time in software using millis().
// Tap the widget to toggle start/stop.
// Long press (> 600ms) resets to zero.
// Format: "mm:ss" (default) or "ss.mmm" (millisecond precision).
//
// The timer state is self-contained in TimerTag — no CAN signal binding.

#include "timer_widget.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <Arduino.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Tag
// ---------------------------------------------------------------------------

namespace {

struct TimerTag {
    lv_obj_t *timeLabel;
    bool running;
    bool formatMsec; // true = "ss.mmm", false = "mm:ss"
    uint32_t startMs;      // millis() at last start
    uint32_t accumulatedMs; // ms accumulated before last pause
    uint32_t pressStartMs; // millis() when touch pressed
    bool longPressFired;
};

static constexpr uint32_t LONG_PRESS_MS = 600;

uint32_t getElapsed(const TimerTag *t) {
    if (!t->running)
        return t->accumulatedMs;
    return t->accumulatedMs + (millis() - t->startMs);
}

void formatTime(char *buf, size_t len, uint32_t elapsedMs, bool msec) {
    if (msec) {
        uint32_t totalS = elapsedMs / 1000;
        uint32_t ms = elapsedMs % 1000;
        snprintf(buf, len, "%02lu.%03lu", (unsigned long)totalS, (unsigned long)ms);
    } else {
        uint32_t totalS = elapsedMs / 1000;
        uint32_t m = totalS / 60;
        uint32_t s = totalS % 60;
        snprintf(buf, len, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
    }
}

void onTimerTouch(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    TimerTag *t = static_cast<TimerTag *>(lv_event_get_user_data(e));
    if (!t)
        return;

    if (code == LV_EVENT_PRESSED) {
        t->pressStartMs = millis();
        t->longPressFired = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (!t->longPressFired && (millis() - t->pressStartMs) >= LONG_PRESS_MS) {
            t->longPressFired = true;
            // Reset
            t->running = false;
            t->accumulatedMs = 0;
            t->startMs = 0;
            LOG_DEBUG("TIMER", "Timer reset");
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (!t->longPressFired) {
            // Short tap: toggle
            if (t->running) {
                t->accumulatedMs = getElapsed(t);
                t->running = false;
                LOG_DEBUG("TIMER", "Timer stopped at %lu ms", (unsigned long)t->accumulatedMs);
            } else {
                t->startMs = millis();
                t->running = true;
                LOG_DEBUG("TIMER", "Timer started");
            }
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

lv_obj_t *TimerWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    if (cfg.style.hasBorder) {
        lv_obj_set_style_border_width(cont, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(cont, lv_color_hex(cfg.style.borderColor.rgb), LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    }
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);

    // Enable tap interaction
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    // Time label
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);

    const lv_font_t *font = &lv_font_montserrat_20;
    if (cfg.layout.h >= 80)
        font = &lv_font_montserrat_24;
    if (cfg.layout.h >= 110)
        font = &lv_font_montserrat_32;
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, cfg.timer.formatMsec ? "00.000" : "00:00");

    // Allocate tag
    TimerTag *tag = new TimerTag{
        label,
        cfg.timer.autoStart,
        cfg.timer.formatMsec,
        cfg.timer.autoStart ? millis() : 0,
        0,
        0,
        false,
    };

    lv_obj_set_user_data(cont, tag);

    // Register touch events for start/stop/reset
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSED, tag);
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSING, tag);
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_RELEASED, tag);

    return cont;
}

void TimerWidget::update(lv_obj_t *obj, float /*value*/, bool /*valid*/, const CfgWidget &cfg) {
    if (!obj)
        return;
    TimerTag *tag = static_cast<TimerTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    // Only rerender while running — paused display is stable
    if (!tag->running)
        return;

    char buf[12];
    uint32_t elapsed = getElapsed(tag);
    formatTime(buf, sizeof(buf), elapsed, tag->formatMsec);
    lv_label_set_text(tag->timeLabel, buf);
    (void)cfg;
}
