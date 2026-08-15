#include "control_button.h"

#include "layout_scale.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

namespace ControlButton {

namespace {

using ControlVocabulary::ControlState;
using ControlVocabulary::kStateCount;

constexpr int16_t kPadX = 7;
constexpr int16_t kPadY = 6;
constexpr int16_t kKickerGapPx = 3;
constexpr int16_t kSegmentTopGapPx = 1;
constexpr int16_t kSegmentHeightPx = 2;
constexpr int16_t kSegmentGapPx = 2;
constexpr int16_t kBorderWidthPx = 2;
constexpr uint8_t kKickerFontPx = 10;
constexpr uint8_t kWordFontPx = 14;
constexpr int16_t kKickerTrackingPx = 2;

constexpr lv_opa_t kKickerEngagedOpa = 191;
constexpr lv_opa_t kSegmentEngagedLitOpa = 77;
constexpr lv_opa_t kPulseMinOpa = 89;
constexpr uint32_t kPulsePeriodMs = 1000;

enum class Tone : uint8_t { INK, DIM, WHITE, ENGAGED, LOCK_LINE, LOCK_INK };

struct StatePaint {
    Tone border;
    Tone ground;
    lv_opa_t groundOpa;
    Tone kicker;
    lv_opa_t kickerOpa;
    Tone word;
    bool pulses;
};

constexpr StatePaint kStatePaint[kStateCount] = {
    {Tone::INK, Tone::INK, LV_OPA_TRANSP, Tone::DIM, LV_OPA_COVER, Tone::INK, false},
    {Tone::INK, Tone::INK, LV_OPA_TRANSP, Tone::DIM, LV_OPA_COVER, Tone::INK, true},
    {Tone::ENGAGED, Tone::ENGAGED, LV_OPA_COVER, Tone::WHITE, kKickerEngagedOpa, Tone::WHITE,
     false},
    {Tone::LOCK_LINE, Tone::INK, LV_OPA_TRANSP, Tone::LOCK_INK, LV_OPA_COVER, Tone::LOCK_INK,
     false},
};

uint32_t inkRgb() {
    return ThemeManager::getEffectiveTextColor();
}

uint32_t whiteRgb() {
    return ThemeTokens::kInkNight;
}

uint32_t engagedRgb() {
    return ThemeTokens::kEngaged;
}

using ToneResolver = uint32_t (*)();

uint32_t toneRgb(Tone tone) {
    static const ToneResolver resolvers[] = {
        inkRgb,     ThemeManager::dimColor,      whiteRgb,
        engagedRgb, ThemeManager::lockLineColor, ThemeManager::lockInkColor};
    return resolvers[static_cast<uint8_t>(tone)]();
}

const StatePaint &paintFor(ControlState state) {
    const uint8_t idx = static_cast<uint8_t>(state);
    return kStatePaint[idx < kStateCount ? idx : 0];
}

void pulseAnimCb(void *obj, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void applyPulse(lv_obj_t *box, bool on) {
    lv_anim_del(box, pulseAnimCb);
    if (!on) {
        lv_obj_set_style_opa(box, LV_OPA_COVER, LV_PART_MAIN);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, box);
    lv_anim_set_exec_cb(&a, pulseAnimCb);
    lv_anim_set_values(&a, LV_OPA_COVER, kPulseMinOpa);
    lv_anim_set_time(&a, kPulsePeriodMs / 2);
    lv_anim_set_playback_time(&a, kPulsePeriodMs / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

lv_obj_t *makeLabel(lv_obj_t *parent, uint8_t fontPx, int16_t trackingPx) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_font(label, FontManager::label(fontPx), 0);
    lv_obj_set_style_text_letter_space(label, trackingPx, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    return label;
}

void buildSegments(lv_obj_t *box, Surface &surface) {
    lv_obj_t *row = lv_obj_create(box);
    WidgetHelpers::resetContainerStyle(row);
    WidgetHelpers::disableInteract(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(row, LayoutScale::y(kSegmentTopGapPx), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, LayoutScale::x(kSegmentGapPx), 0);
    for (uint8_t i = 0; i < CONTROL_STEP_MAX; ++i) {
        lv_obj_t *cell = lv_obj_create(row);
        WidgetHelpers::resetContainerStyle(cell);
        WidgetHelpers::disableInteract(cell);
        lv_obj_set_height(cell, LayoutScale::y(kSegmentHeightPx));
        lv_obj_set_flex_grow(cell, 1);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        surface.segments[i] = cell;
    }
    surface.hasSegments = true;
}

void paintSegments(const Surface &surface, ControlState state, uint8_t level) {
    if (!surface.hasSegments)
        return;
    const bool engaged = state == ControlState::ACTIVE;
    const uint32_t litRgb =
        engaged ? ThemeTokens::kInkNight : ThemeManager::getEffectiveTextColor();
    const lv_opa_t litOpa = engaged ? kSegmentEngagedLitOpa : LV_OPA_COVER;
    const uint32_t unlitRgb = ThemeManager::trackColor();
    for (uint8_t i = 0; i < CONTROL_STEP_MAX; ++i) {
        const bool lit = i < level;
        lv_obj_set_style_bg_color(surface.segments[i], lv_color_hex(lit ? litRgb : unlitRgb), 0);
        lv_obj_set_style_bg_opa(surface.segments[i], lit ? litOpa : LV_OPA_COVER, 0);
    }
}

} // namespace

Surface build(lv_obj_t *box, bool withSegments) {
    Surface surface = {};
    surface.box = box;
    surface.painted = ControlState::OFF;

    lv_obj_set_style_radius(box, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, kBorderWidthPx, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(box, LayoutScale::x(kPadX), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(box, LayoutScale::y(kPadY), LV_PART_MAIN);
    lv_obj_set_style_pad_row(box, LayoutScale::y(kKickerGapPx), LV_PART_MAIN);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    surface.kicker = makeLabel(box, kKickerFontPx, kKickerTrackingPx);
    surface.word = makeLabel(box, kWordFontPx, 0);
    if (withSegments)
        buildSegments(box, surface);
    return surface;
}

void setTexts(const Surface &surface, const char *kicker, const char *word) {
    WidgetHelpers::setLabelTextIfChanged(surface.kicker, kicker);
    WidgetHelpers::setLabelTextIfChanged(surface.word, word);
}

void paint(Surface &surface, ControlState state, uint8_t level) {
    if (!surface.box)
        return;
    if (surface.everPainted && surface.painted == state && surface.paintedLevel == level)
        return;
    const StatePaint &p = paintFor(state);
    lv_obj_set_style_border_color(surface.box, lv_color_hex(toneRgb(p.border)), LV_PART_MAIN);
    lv_obj_set_style_border_width(surface.box, kBorderWidthPx, LV_PART_MAIN);
    lv_obj_set_style_bg_color(surface.box, lv_color_hex(toneRgb(p.ground)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(surface.box, p.groundOpa, LV_PART_MAIN);
    lv_obj_set_style_text_color(surface.kicker, lv_color_hex(toneRgb(p.kicker)), 0);
    lv_obj_set_style_text_opa(surface.kicker, p.kickerOpa, 0);
    lv_obj_set_style_text_color(surface.word, lv_color_hex(toneRgb(p.word)), 0);
    paintSegments(surface, state, level);
    applyPulse(surface.box, p.pulses);
    surface.painted = state;
    surface.paintedLevel = level;
    surface.everPainted = true;
}

} // namespace ControlButton
