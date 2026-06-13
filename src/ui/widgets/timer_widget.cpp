#include "timer_widget.h"
#include "runtime/timer_service.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"

#include <esp_timer.h>
#include <lvgl.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {

struct TimerTag {
    lv_obj_t *cont;
    lv_obj_t *timeLabel;
    bool formatMsec;
    uint32_t pressStartMs;
    bool longPressFired;
    TimerService::State lastState;
    uint32_t textRgb;
    char lastText[12];
};

constexpr uint32_t LONG_PRESS_MS = 600;
constexpr uint32_t BLINK_PERIOD_MS = 1000;

constexpr uint32_t kRunningBorderRgb = WidgetHelpers::kZoneNormalRgb;
constexpr uint32_t kPausedBorderRgb = WidgetHelpers::kZoneWarningRgb;
constexpr uint8_t kStateBorderWidth = 2;
constexpr lv_opa_t kResetTextOpa = LV_OPA_60;

void formatTime(char *buf, size_t len, uint32_t elapsedMs, bool msec, bool blinkOn) {
    if (msec) {
        const uint32_t totalS = elapsedMs / 1000;
        const uint32_t ms = elapsedMs % 1000;
        snprintf(buf, len, "%02lu.%03lu", static_cast<unsigned long>(totalS),
                 static_cast<unsigned long>(ms));
        return;
    }
    const uint32_t totalS = elapsedMs / 1000;
    const uint32_t m = totalS / 60;
    const uint32_t s = totalS % 60;
    const char sep = blinkOn ? ':' : ' ';
    snprintf(buf, len, "%02lu%c%02lu", static_cast<unsigned long>(m), sep,
             static_cast<unsigned long>(s));
}

void applyStateStyle(TimerTag *t, TimerService::State state) {
    if (!t || !t->cont || !t->timeLabel)
        return;

    switch (state) {
        case TimerService::State::Reset:
            lv_obj_set_style_border_width(t->cont, 0, 0);
            lv_obj_set_style_text_color(t->timeLabel, lv_color_hex(t->textRgb), 0);
            lv_obj_set_style_text_opa(t->timeLabel, kResetTextOpa, 0);
            break;
        case TimerService::State::Running:
            lv_obj_set_style_border_width(t->cont, kStateBorderWidth, 0);
            lv_obj_set_style_border_color(t->cont, lv_color_hex(kRunningBorderRgb), 0);
            lv_obj_set_style_border_opa(t->cont, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(t->timeLabel, lv_color_hex(t->textRgb), 0);
            lv_obj_set_style_text_opa(t->timeLabel, LV_OPA_COVER, 0);
            break;
        case TimerService::State::Paused:
            lv_obj_set_style_border_width(t->cont, kStateBorderWidth, 0);
            lv_obj_set_style_border_color(t->cont, lv_color_hex(kPausedBorderRgb), 0);
            lv_obj_set_style_border_opa(t->cont, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(t->timeLabel, lv_color_hex(t->textRgb), 0);
            lv_obj_set_style_text_opa(t->timeLabel, LV_OPA_COVER, 0);
            break;
    }
}

void onTimerTouch(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    auto *t = static_cast<TimerTag *>(lv_event_get_user_data(e));
    if (!t)
        return;

    if (code == LV_EVENT_PRESSED) {
        t->pressStartMs = millis();
        t->longPressFired = false;
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (!t->longPressFired && (millis() - t->pressStartMs) >= LONG_PRESS_MS) {
            t->longPressFired = true;
            (void)TimerService::reset();
        }
        return;
    }

    if (code == LV_EVENT_RELEASED) {
        if (t->longPressFired)
            return;
        switch (TimerService::getState()) {
            case TimerService::State::Reset:
                (void)TimerService::start();
                break;
            case TimerService::State::Running:
                (void)TimerService::pause();
                break;
            case TimerService::State::Paused:
                (void)TimerService::resume();
                break;
        }
    }
}

} // namespace

void TimerWidget::reapplyTheme(lv_obj_t *obj, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *t = static_cast<TimerTag *>(lv_obj_get_user_data(obj));
    if (!t || !t->timeLabel)
        return;
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    if (textRgb == t->textRgb)
        return;
    t->textRgb = textRgb;
    lv_obj_set_style_text_color(t->timeLabel, lv_color_hex(textRgb), 0);
}

lv_obj_t *TimerWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);

    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    lv_obj_t *label = lv_label_create(cont);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);

    const lv_font_t *font = FontManager::secondary(20);
    if (cfg.layout.h >= 80)
        font = FontManager::secondary(24);
    if (cfg.layout.h >= 110)
        font = FontManager::primary(32);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, cfg.timer.formatMsec ? "00.000" : "00:00");

    WidgetTagPool::Slot<TimerTag> tagSlot;
    TimerTag *tag = tagSlot.get();
    if (!tag) {
        LOG_WARN("TMR", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(cont);
        return nullptr;
    }
    tag->cont = cont;
    tag->timeLabel = label;
    tag->formatMsec = cfg.timer.formatMsec;
    tag->pressStartMs = 0;
    tag->longPressFired = false;
    tag->lastState = TimerService::State::Reset;
    tag->textRgb = textRgb;

    strlcpy(tag->lastText, cfg.timer.formatMsec ? "00.000" : "00:00", sizeof(tag->lastText));

    applyStateStyle(tag, TimerService::State::Reset);

    if (cfg.timer.autoStart) {
        (void)TimerService::start();
    }

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<TimerTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSED, tag);
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSING, tag);
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_RELEASED, tag);

    WidgetLabelOverlay::applySignalHeader(cont, "timer");

    return cont;
}

void TimerWidget::update(lv_obj_t *obj, float, bool, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<TimerTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    const TimerService::Snapshot snap = TimerService::snapshot();

    if (snap.state != tag->lastState) {
        applyStateStyle(tag, snap.state);

        tag->lastText[0] = '\0';
        tag->lastState = snap.state;
    }

    bool blinkOn = true;
    if (snap.state == TimerService::State::Paused && !tag->formatMsec) {
        const uint32_t phase = static_cast<uint32_t>(
            static_cast<uint64_t>(esp_timer_get_time() / 1000) % BLINK_PERIOD_MS);
        blinkOn = (phase < (BLINK_PERIOD_MS / 2));
    }

    char buf[12];
    formatTime(buf, sizeof(buf), snap.elapsedMs, tag->formatMsec, blinkOn);

    if (strcmp(tag->lastText, buf) != 0) {
        lv_label_set_text(tag->timeLabel, buf);
        strlcpy(tag->lastText, buf, sizeof(tag->lastText));
    }
    (void)cfg;
}
