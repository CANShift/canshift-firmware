#include "timer_widget.h"
#include "runtime/timer_service.h"
#include "app_config.h"
#include "runtime/track_store.h"
#include "runtime/imu_feed.h"
#include "config/config_loader.h"
#include "ui/widgets/timer_sources.h"
#include "ui/font_manager.h"
#include "ui/widgets/value_cluster.h"
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
constexpr uint8_t kSmallTimeFontPx = 17;
constexpr uint8_t kMediumTimeFontPx = 22;
constexpr uint8_t kLargeTimeFontPx = 40;
constexpr int kMediumTimeMinH = 28;
constexpr int kLargeTimeMinH = 55;
constexpr int kLargeTimeMinW = 150;
constexpr uint32_t kBlinkPeriodMs = 1000;

constexpr uint32_t kRunningBorderRgb = WidgetHelpers::kAccentRgb;
constexpr uint32_t kPausedBorderRgb = WidgetHelpers::kMutedRgb;
constexpr uint8_t kStateBorderWidth = 2;
constexpr lv_opa_t kResetTextOpa = LV_OPA_60;
constexpr lv_opa_t kLapBadgeOpa = LV_OPA_80;
constexpr int16_t kLapBadgeInsetPx = 2;

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

using TimerCommand = bool (*)();

bool noTimerCommand() {
    return true;
}

constexpr TimerCommand kTapCommands[] = {TimerService::start, TimerService::lap,
                                         TimerService::resume};
constexpr TimerCommand kLongPressCommands[] = {noTimerCommand, TimerService::pause,
                                               TimerService::reset};

void runForState(const TimerCommand (&commands)[3], const char *gesture) {
    const TimerService::State state = TimerService::getState();
    if (!commands[static_cast<uint8_t>(state)]())
        LOG_DEBUG("TMR", "%s rejected in state %u", gesture, static_cast<unsigned>(state));
}

void onTimerPressing(TimerTag *t) {
    if (t->longPressFired || millis() - t->pressStartMs < kLongPressMs)
        return;
    t->longPressFired = true;
    runForState(kLongPressCommands, "long press");
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
        onTimerPressing(t);
        return;
    }
    if (code == LV_EVENT_RELEASED && !t->longPressFired)
        runForState(kTapCommands, "tap");
}

uint8_t timeFontPxFor(const CfgWidget &cfg) {
    if (cfg.timer.big > 0)
        return WidgetHelpers::deviceFontPxForBig(cfg.timer.big);
    const WidgetHelpers::ScaledBox box = WidgetHelpers::scaledBox(cfg);
    if (box.h >= kLargeTimeMinH && box.w >= kLargeTimeMinW)
        return kLargeTimeFontPx;
    if (box.h >= kMediumTimeMinH)
        return kMediumTimeFontPx;
    return kSmallTimeFontPx;
}

const char *zeroTimeText(const CfgWidget &cfg) {
    return cfg.timer.formatMsec ? "00.000" : "00:00";
}

lv_obj_t *makeTimeLabel(lv_obj_t *cont, const CfgWidget &cfg, uint32_t textRgb) {
    const uint8_t fontPx = timeFontPxFor(cfg);
    lv_obj_t *row = ValueCluster::makeRow(cont);
    lv_obj_t *label = ValueCluster::makeValue(row, fontPx);
    if (!label)
        return nullptr;
    lv_obj_set_style_text_color(label, lv_color_hex(textRgb), 0);
    lv_label_set_text(label, zeroTimeText(cfg));
    ValueCluster::makeUnit(row, TimerSources::unit(cfg.timer.source));
    ValueCluster::makeRule(cont, fontPx, textRgb);
    return label;
}

lv_obj_t *makeLapBadge(lv_obj_t *cont, uint32_t textRgb) {
    lv_obj_t *lapLabel = lv_label_create(cont);
    lv_obj_align(lapLabel, LV_ALIGN_TOP_RIGHT, -LayoutScale::x(kLapBadgeInsetPx),
                 LayoutScale::y(kLapBadgeInsetPx));
    lv_obj_set_style_text_font(lapLabel, FontManager::units(), 0);
    lv_obj_set_style_text_color(lapLabel, lv_color_hex(textRgb), 0);
    lv_obj_set_style_text_opa(lapLabel, kLapBadgeOpa, 0);
    lv_label_set_text(lapLabel, "");
    lv_obj_add_flag(lapLabel, LV_OBJ_FLAG_HIDDEN);
    return lapLabel;
}

bool initTimerTag(TimerTag *tag, const CfgWidget &cfg, lv_obj_t *cont, uint32_t textRgb) {
    tag->cont = cont;
    tag->timeLabel = makeTimeLabel(cont, cfg, textRgb);
    if (!tag->timeLabel)
        return false;
    tag->lapLabel = makeLapBadge(cont, textRgb);
    tag->formatMsec = cfg.timer.formatMsec;
    tag->source = cfg.timer.source;
    tag->pressStartMs = 0;
    tag->longPressFired = false;
    tag->lastState = TimerService::State::Reset;
    tag->lastLapCount = 0;
    tag->textRgb = textRgb;
    strlcpy(tag->lastText, zeroTimeText(cfg), sizeof(tag->lastText));
    return true;
}

void makeInteractive(lv_obj_t *cont, TimerTag *tag) {
    applyStateStyle(tag, TimerService::State::Reset);
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSED, tag);
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_PRESSING, tag);
    lv_obj_add_event_cb(cont, onTimerTouch, LV_EVENT_RELEASED, tag);
}

}

lv_obj_t *TimerWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::initContainer(cont, cfg, yOffset, cfg.style.hasBorder,
                                 cfg.style.borderColor.rgb);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    WidgetTagPool::Slot<TimerTag> tagSlot;
    TimerTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "TMR", cont);
    if (!tag)
        return nullptr;
    const uint32_t textRgb =
        ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
    if (!initTimerTag(tag, cfg, cont, textRgb)) {
        LOG_ERROR("TMR", "time label create failed for '%s' — LVGL pool OOM", cfg.id);
        lv_obj_del(cont);
        return nullptr;
    }
    if (cfg.timer.autoStart && !TimerService::start())
        LOG_DEBUG("TMR", "auto-start rejected for '%s'", cfg.id);
    lv_obj_set_user_data(cont, tag);
    lv_obj_add_event_cb(cont, WidgetTagPool::deleteHandler<TimerTag>, LV_EVENT_DELETE,
                        tagSlot.commit());
    if (TimerSources::isInteractive(cfg.timer.source))
        makeInteractive(cont, tag);
    WidgetLabelOverlay::applySignalHeader(
        cont, TimerSources::kicker(cfg.timer.source, ConfigLoader::getDashboardConfig().units ==
                                                         CfgUnitSystem::IMPERIAL));
    return cont;
}

namespace {

TimerSources::Inputs gatherInputs(const TimerService::Snapshot &snap) {
    TrackStore::State track;
    TrackStore::snapshot(&track);
    TimerSources::Inputs in =
        TimerSources::select(snap, track, TrackStore::isActiveWithin(kTrackTelemetryTimeoutMs));
    LaunchTimer launch = {};
    if (ImuFeed::latestLaunch(launch))
        in.launch = TimerSources::launchReading(launch, millis());
    return in;
}

void updateReadout(TimerTag *tag, const TimerService::Snapshot &snap) {
    char buf[TimerSources::kTextCapacity];
    const bool reading = TimerSources::render(tag->source, gatherInputs(snap), buf, sizeof(buf));
    if (strcmp(tag->lastText, buf) == 0)
        return;
    const uint32_t rgb = reading ? tag->textRgb : ThemeManager::getStaleTextColor();
    lv_obj_set_style_text_color(tag->timeLabel, lv_color_hex(rgb), 0);
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
    TimerSources::formatStopwatch(buf, sizeof(buf), snap.elapsedMs, tag->formatMsec,
                                  stopwatchBlinkOn(tag, snap.state));
    if (strcmp(tag->lastText, buf) == 0)
        return;
    lv_label_set_text(tag->timeLabel, buf);
    strlcpy(tag->lastText, buf, sizeof(tag->lastText));
}

}

void TimerWidget::update(lv_obj_t *obj, float, bool, const CfgWidget &) {
    if (!obj)
        return;
    auto *tag = static_cast<TimerTag *>(lv_obj_get_user_data(obj));
    if (!tag)
        return;

    const TimerService::Snapshot snap = TimerService::snapshot();
    if (TimerSources::isInteractive(tag->source)) {
        updateStopwatch(tag, snap);
        return;
    }
    updateReadout(tag, snap);
}
