#include "control_splash.h"
#include "control_splash_internal.h"

#include "layout_scale.h"
#include "ui/alert_takeover.h"
#include "ui/font_manager.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

#include <Arduino.h>

namespace {

using ControlSplashContent::Content;
using ControlSplashCopy::Bottom;
using ControlSplashCopy::Sub;
using ControlSplashCopy::Tone;
using namespace ControlSplashInternal;

using ColorFn = uint32_t (*)();

uint32_t inkRgb() {
    return ThemeManager::getEffectiveTextColor();
}

uint32_t engagedRgb() {
    return ThemeTokens::kEngaged;
}

struct TonePaint {
    ColorFn rule;
    ColorFn kicker;
    ColorFn hero;
    uint8_t heroFontPx;
};

constexpr TonePaint kTonePaint[ControlSplashCopy::kToneCount] = {
    {engagedRgb, engagedRgb, engagedRgb, kHeroFontPx},
    {inkRgb, ThemeManager::dimColor, inkRgb, kHeroFontPx},
    {ThemeManager::dangerColor, ThemeManager::dangerColor, ThemeManager::dangerColor,
     kRefusedHeroFontPx},
};

struct SubStyle {
    bool mono;
    uint8_t fontPx;
    int16_t trackingPx;
    ColorFn ink;
    int16_t gapPx;
};

constexpr SubStyle kSubStyles[] = {{false, 10, 0, ThemeManager::dimColor, 0},
                                   {false, 13, 2, inkRgb, 5},
                                   {false, 11, 2, ThemeManager::dimColor, 2},
                                   {true, 13, 0, inkRgb, 7}};

struct BottomShape {
    bool bottom;
    bool line;
    bool cells;
    bool segments;
};

constexpr BottomShape kBottomShapes[] = {
    {false, false, false, false},
    {true, true, false, false},
    {true, false, true, false},
    {false, false, false, true},
};

Layer s_layer = {};
ControlSplashRs s_timer = {};
bool s_visible = false;

void paintHero(const Content &content, const TonePaint &tone) {
    const lv_font_t *font = FontManager::value(tone.heroFontPx);
    const lv_font_t *unitFont = FontManager::value(kHeroUnitFontPx);
    lv_label_set_text(s_layer.hero, content.hero);
    lv_obj_set_style_text_font(s_layer.hero, font, 0);
    lv_obj_set_style_text_letter_space(s_layer.hero,
                                       WidgetHelpers::valueTrackingPx(tone.heroFontPx), 0);
    lv_obj_set_style_text_color(s_layer.hero, lv_color_hex(tone.hero()), 0);
    lv_label_set_text(s_layer.heroUnit, content.heroUnit);
    lv_obj_set_style_pad_bottom(s_layer.heroUnit, baselineDropPx(font, unitFont), 0);
    WidgetHelpers::setVisible(s_layer.heroUnit, content.heroUnit[0] != '\0');
}

void paintSub(const Content &content) {
    const SubStyle &style = kSubStyles[static_cast<uint8_t>(content.sub)];
    WidgetHelpers::setVisible(s_layer.sub, content.sub != Sub::NONE);
    lv_label_set_text(s_layer.sub, content.subText);
    lv_obj_set_style_text_font(
        s_layer.sub,
        style.mono ? FontManager::value(style.fontPx) : FontManager::label(style.fontPx), 0);
    lv_obj_set_style_text_letter_space(s_layer.sub, style.trackingPx, 0);
    lv_obj_set_style_text_color(s_layer.sub, lv_color_hex(style.ink()), 0);
    lv_obj_set_style_pad_top(s_layer.sub, LayoutScale::y(style.gapPx), 0);
}

void paintCells(const Content &content) {
    for (uint8_t i = 0; i < ControlSplashContent::kMaxCells; ++i) {
        const bool used = i < content.cellCount;
        WidgetHelpers::setVisible(lv_obj_get_parent(s_layer.cellKicker[i]), used);
        if (!used)
            continue;
        lv_label_set_text(s_layer.cellKicker[i], content.cells[i].kicker);
        lv_label_set_text(s_layer.cellValue[i], content.cells[i].value);
        lv_label_set_text(s_layer.cellUnit[i], content.cells[i].unit);
    }
}

void paintSegments(const Content &content, const TonePaint &tone) {
    const uint32_t litRgb = tone.hero();
    const uint32_t unlitRgb = ThemeManager::trackColor();
    for (uint8_t i = 0; i < CONTROL_STEP_MAX; ++i) {
        const bool lit = i < content.segmentsLit;
        lv_obj_set_style_bg_color(s_layer.segment[i], lv_color_hex(lit ? litRgb : unlitRgb),
                                  LV_PART_MAIN);
    }
}

void paintBottom(const Content &content, const TonePaint &tone) {
    const BottomShape &shape = kBottomShapes[static_cast<uint8_t>(content.bottom)];
    WidgetHelpers::setVisible(s_layer.bottom, shape.bottom);
    WidgetHelpers::setVisible(s_layer.line, shape.line);
    WidgetHelpers::setVisible(s_layer.cells, shape.cells);
    WidgetHelpers::setVisible(s_layer.segments, shape.segments);
    lv_label_set_text(s_layer.line, content.line);
    paintCells(content);
    paintSegments(content, tone);
}

void paint(const Content &content) {
    const TonePaint &tone = kTonePaint[static_cast<uint8_t>(content.tone)];
    lv_obj_set_style_bg_color(s_layer.rule, lv_color_hex(tone.rule()), LV_PART_MAIN);
    lv_label_set_text(s_layer.kicker, content.name);
    lv_obj_set_style_text_color(s_layer.kicker, lv_color_hex(tone.kicker()), 0);
    paintHero(content, tone);
    paintSub(content);
    paintBottom(content, tone);
}

void show(bool visible) {
    if (visible == s_visible)
        return;
    s_visible = visible;
    WidgetHelpers::setVisible(s_layer.root, visible);
}

bool ensureBuilt() {
    if (s_layer.root)
        return true;
    if (!ControlSplashInternal::build(s_layer)) {
        s_layer = {};
        return false;
    }
    s_visible = true;
    show(false);
    return true;
}

} // namespace

void ControlSplash::raiseFor(const ControlVocabulary::Control &control,
                             ControlVocabulary::ControlState state, uint8_t level) {
    if (AlertTakeover::isActive())
        return;
    if (!ensureBuilt())
        return;
    Content content;
    ControlSplashContent::compose(control, state, level, content);
    const uint8_t kind =
        content.tone == Tone::REFUSE ? CONTROL_SPLASH_KIND_REFUSAL : CONTROL_SPLASH_KIND_CHANGE;
    paint(content);
    control_splash_raise_rs(&s_timer, kind, millis());
    show(true);
}

void ControlSplash::update() {
    if (!s_layer.root)
        return;
    if (AlertTakeover::isActive()) {
        control_splash_preempt_rs(&s_timer);
        show(false);
        return;
    }
    show(control_splash_poll_rs(&s_timer, millis()));
}
