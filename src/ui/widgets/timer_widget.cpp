#include "timer_widget.h"
#include "runtime/timer_service.h"
#include "app_config.h"
#include "runtime/track_store.h"
#include "ui/widgets/timer_sources.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/widget_label.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "layout_scale.h"
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
    lv_obj_t *lapLabel;
    bool formatMsec;
    CfgTimerSource source;
    uint32_t pressStartMs;
    bool longPressFired;
    TimerService::State lastState;
    uint16_t lastLapCount;
    uint32_t textRgb;
    char lastText[12];
};

constexpr uint32_t kLongPressMs = 600;
constexpr uint32_t kBlinkPeriodMs = 1000;

constexpr uint32_t kRunningBorderRgb = WidgetHelpers::kAccentRgb;
constexpr uint32_t kPausedBorderRgb = WidgetHelpers::kMutedRgb;
constexpr uint8_t kStateBorderWidth = 2;
constexpr lv_opa_t kResetTextOpa = LV_OPA_60;
constexpr lv_opa_t kLapBadgeOpa = LV_OPA_80;
constexpr int16_t kLapBadgeInsetPx = 2;

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
        if (!t->longPressFired && (millis() - t->pressStartMs) >= kLongPressMs) {
            t->longPressFired = true;
            switch (TimerService::getState()) {
                case TimerService::State::Running:
                    (void)TimerService::pause();
                    break;
                case TimerService::State::Paused:
                    (void)TimerService::reset();
                    break;
                case TimerService::State::Reset:
                    break;
            }
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
                (void)TimerService::lap();
                break;
            case TimerService::State::Paused:
                (void)TimerService::resume();
                break;
        }
    }
}

} // namespace

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

    const lv_font_t *font = FontManager::value(17);
    if (cfg.timer.big > 0) {
        font = FontManager::value(WidgetHelpers::deviceFontPxForBig(cfg.timer.big));
    } else {
        const WidgetHelpers::ScaledBox box = WidgetHelpers::scaledBox(cfg);
        if (box.h >= 28)
            font = FontManager::value(22);
        if (box.h >= 55 && box.w >= 150)
            font = FontManager::value(40);
    }
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, cfg.timer.formatMsec ? "00.000" : "00:00");

    lv_obj_t *lapLabel = lv_label_create(cont);
    lv_obj_align(lapLabel, LV_ALIGN_TOP_RIGHT, -LayoutScale::x(kLapBadgeInsetPx),
                 LayoutScale::y(kLapBadgeInsetPx));
    lv_obj_set_style_text_font(lapLabel, FontManager::units(), 0);
    lv_obj_set_style_text_color(lapLabel, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_opa(lapLabel, kLapBadgeOpa, 0);
    lv_label_set_text(lapLabel, "");
    lv_obj_add_flag(lapLabel, LV_OBJ_FLAG_HIDDEN);

    WidgetTagPool::Slot<TimerTag> tagSlot;
    TimerTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "TMR", cont);
    if (!tag)
        return nullptr;
    tag->cont = cont;
    tag->timeLabel = label;
    tag->lapLabel = lapLabel;
    tag->formatMsec = cfg.timer.formatMsec;
    tag->source = cfg.timer.source;
    tag->pressStartMs = 0;
    tag->longPressFired = false;
    tag->lastState = TimerService::State::Reset;
    tag->lastLapCount = 0;
    tag->textRgb = textRgb;

    strlcpy(tag->lastText, cfg.timer.formatMsec ? "00.000" : "00:00", sizeof(tag->lastText));

    applyStateStyle(tag, TimerService::State::Reset);

    if (cfg.timer.autoStart) {
        (void)TimerService::start();
    }

    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<TimerTag>, LV_EVENT_DELETE,
                        tagSlot.commit());

    if (TimerSources::isInteractive(cfg.timer.source)) {
        lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSED, tag);
        lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSING, tag);
        lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_RELEASED, tag);
    }

    WidgetLabelOverlay::applySignalHeader(cont, TimerSources::kicker(cfg.timer.source));

    return cont;
}

namespace {

TimerSources::Inputs gatherInputs(const TimerService::Snapshot &snap) {
    TrackStore::State track;
    TrackStore::snapshot(&track);
    return {snap.elapsedMs,     snap.lapCount,
            track.currentLapMs, track.lastLapMs,
            track.bestLapMs,    track.lapNumber,
            track.deltaMs,      TrackStore::isActiveWithin(TRACK_TELEMETRY_TIMEOUT_MS)};
}

void updateReadout(TimerTag *tag, const TimerService::Snapshot &snap) {
    char buf[TimerSources::kTextCapacity];
    TimerSources::render(tag->source, gatherInputs(snap), buf, sizeof(buf));
    if (strcmp(tag->lastText, buf) == 0)
        return;
    lv_label_set_text(tag->timeLabel, buf);
    strlcpy(tag->lastText, buf, sizeof(tag->lastText));
}

void updateLapBadge(TimerTag *tag, uint16_t lapCount) {
    if (lapCount == tag->lastLapCount)
        return;
    tag->lastLapCount = lapCount;
    if (lapCount == 0) {
        lv_obj_add_flag(tag->lapLabel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char lapBuf[8];
    snprintf(lapBuf, sizeof(lapBuf), "L%u", static_cast<unsigned>(lapCount));
    lv_label_set_text(tag->lapLabel, lapBuf);
    lv_obj_clear_flag(tag->lapLabel, LV_OBJ_FLAG_HIDDEN);
}

bool stopwatchBlinkOn(const TimerTag *tag, TimerService::State state) {
    if (state != TimerService::State::Paused || tag->formatMsec)
        return true;
    const uint32_t phase =
        static_cast<uint32_t>(static_cast<uint64_t>(esp_timer_get_time() / 1000) % kBlinkPeriodMs);
    return phase < (kBlinkPeriodMs / 2);
}

void updateStopwatch(TimerTag *tag, const TimerService::Snapshot &snap) {
    if (snap.state != tag->lastState) {
        applyStateStyle(tag, snap.state);
        tag->lastText[0] = '\0';
        tag->lastState = snap.state;
    }
    updateLapBadge(tag, snap.lapCount);

    char buf[12];
    formatTime(buf, sizeof(buf), snap.elapsedMs, tag->formatMsec,
               stopwatchBlinkOn(tag, snap.state));
    if (strcmp(tag->lastText, buf) == 0)
        return;
    lv_label_set_text(tag->timeLabel, buf);
    strlcpy(tag->lastText, buf, sizeof(tag->lastText));
}

} // namespace

void TimerWidget::update(lv_obj_t *obj, float, bool, const CfgWidget &cfg) {
    if (!obj)
        return;
    auto *tag = static_cast<TimerTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    const TimerService::Snapshot snap = TimerService::snapshot();
    if (TimerSources::isInteractive(tag->source)) {
        updateStopwatch(tag, snap);
        (void)cfg;
        return;
    }
    updateReadout(tag, snap);
    (void)cfg;
}
