#include "alert_takeover.h"
#include "ui/alert_sources.h"
#include "ui/ota_overlay.h"
#include "ui/font_manager.h"
#include "ui/widgets/widget_helpers.h"
#include "runtime/signal_store.h"
#include "util/format_float.h"
#include "layout_scale.h"

#include <Arduino.h>
#include <atomic>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t kUpdatePeriodMs = 100;
constexpr uint32_t kPulsePeriodMs = 1000;
constexpr lv_opa_t kPulseMinOpa = 0x59;
constexpr int16_t kEdgePadPx = 22;
constexpr int16_t kNameTrackingPx = 4;
constexpr int16_t kStopTrackingPx = 3;
constexpr int16_t kValueYOffsetPx = -10;
constexpr int kNoActiveSource = -1;

lv_obj_t *s_root = nullptr;
lv_obj_t *s_bg = nullptr;
lv_obj_t *s_name = nullptr;
lv_obj_t *s_value = nullptr;
lv_obj_t *s_stop = nullptr;

uint32_t s_lastUpdateMs = 0;
int s_activeSource = kNoActiveSource;
int s_acknowledgedSource = kNoActiveSource;
std::atomic<bool> s_activeFlag{false};
std::atomic<bool> s_ackRequested{false};
char s_lastValueText[24] = "";

void pulseAnimCb(void *obj, int32_t value) {
    lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value),
                            LV_PART_MAIN);
}

void startPulse(lv_obj_t *bg) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bg);
    lv_anim_set_exec_cb(&a, pulseAnimCb);
    lv_anim_set_values(&a, LV_OPA_COVER, kPulseMinOpa);
    lv_anim_set_time(&a, kPulsePeriodMs / 2);
    lv_anim_set_playback_time(&a, kPulsePeriodMs / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void ackClickCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    s_ackRequested.store(true, std::memory_order_relaxed);
}

void buildLayer() {
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_root, ackClickCb, LV_EVENT_CLICKED, nullptr);

    s_bg = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_bg);
    lv_obj_set_size(s_bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_bg, lv_color_hex(WidgetHelpers::kZoneDangerRgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bg, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    startPulse(s_bg);

    s_name = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_name, FontManager::label(16), 0);
    lv_obj_set_style_text_color(s_name, lv_color_hex(0xFFFFFFu), 0);
    lv_obj_set_style_text_letter_space(s_name, LayoutScale::x(kNameTrackingPx), 0);
    lv_obj_align(s_name, LV_ALIGN_TOP_LEFT, LayoutScale::x(kEdgePadPx), LayoutScale::y(kEdgePadPx));

    s_value = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_value, FontManager::value(48), 0);
    lv_obj_set_style_text_color(s_value, lv_color_hex(0xFFFFFFu), 0);
    lv_obj_set_style_text_letter_space(s_value, WidgetHelpers::valueTrackingPx(48), 0);
    lv_obj_align(s_value, LV_ALIGN_LEFT_MID, LayoutScale::x(kEdgePadPx),
                 LayoutScale::y(kValueYOffsetPx));
    lv_label_set_text(s_value, "");

    s_stop = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_stop, FontManager::label(16), 0);
    lv_obj_set_style_text_color(s_stop, lv_color_hex(0xFFFFFFu), 0);
    lv_obj_set_style_text_letter_space(s_stop, LayoutScale::x(kStopTrackingPx), 0);
    lv_label_set_text(s_stop, "STOP THE ENGINE");
    lv_obj_align(s_stop, LV_ALIGN_BOTTOM_MID, 0, -LayoutScale::y(kEdgePadPx));
}

int pickCriticalSource(const AlertEngine::AlertState &state) {
    for (size_t i = 0; i < AlertSources::kSourceCount; ++i) {
        const AlertSources::CriticalSource &src = AlertSources::kSources[i];
        if (state.*(src.level) != AlertEngine::AlertLevel::CRITICAL)
            continue;
        if (state.*(src.sensorLost) && !SignalStore::isValid(src.id))
            continue;
        return static_cast<int>(i);
    }
    return kNoActiveSource;
}

void showFor(int sourceIndex) {
    const AlertSources::CriticalSource &src = AlertSources::kSources[sourceIndex];
    lv_label_set_text(s_name, src.takeoverName);
    s_lastValueText[0] = '\0';
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void hideLayer() {
    if (s_activeSource == kNoActiveSource)
        return;
    s_activeSource = kNoActiveSource;
    s_activeFlag.store(false, std::memory_order_relaxed);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void refreshValue(const AlertSources::CriticalSource &src) {
    char valBuf[16];
    FloatFormat::formatFixed(valBuf, sizeof(valBuf), SignalStore::read(src.id), src.decimals);
    char text[24];
    snprintf(text, sizeof(text), "%s %s", valBuf, src.unit);
    if (strcmp(text, s_lastValueText) == 0)
        return;
    strlcpy(s_lastValueText, text, sizeof(s_lastValueText));
    lv_label_set_text(s_value, text);
}

} // namespace

void AlertTakeover::init() {
    buildLayer();
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_lastUpdateMs = 0;
    s_activeSource = kNoActiveSource;
    s_acknowledgedSource = kNoActiveSource;
}

void AlertTakeover::update() {
    if (!s_root)
        return;

    const uint32_t now = millis();
    if (now - s_lastUpdateMs < kUpdatePeriodMs)
        return;
    s_lastUpdateMs = now;

    if (s_ackRequested.exchange(false, std::memory_order_relaxed) &&
        s_activeSource != kNoActiveSource) {
        s_acknowledgedSource = s_activeSource;
    }

    const AlertEngine::AlertState state = AlertEngine::getState();
    const int critical = OtaOverlay::isActive() ? kNoActiveSource : pickCriticalSource(state);

    if (critical == kNoActiveSource) {
        s_acknowledgedSource = kNoActiveSource;
        hideLayer();
        return;
    }

    if (critical == s_acknowledgedSource) {
        hideLayer();
        return;
    }

    if (critical != s_activeSource) {
        s_activeSource = critical;
        s_activeFlag.store(true, std::memory_order_relaxed);
        showFor(critical);
    }
    refreshValue(AlertSources::kSources[critical]);
}

bool AlertTakeover::isActive() {
    return s_activeFlag.load(std::memory_order_relaxed);
}

void AlertTakeover::requestAcknowledge() {
    s_ackRequested.store(true, std::memory_order_relaxed);
}
