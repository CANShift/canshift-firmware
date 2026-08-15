#include "control_splash_internal.h"

#include "layout_scale.h"
#include "ui/font_manager.h"
#include "ui/overlay_scaffold.h"
#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"

namespace ControlSplashInternal {

namespace {

lv_obj_t *makeColumn(lv_obj_t *parent, int16_t topGapPx) {
    lv_obj_t *col = lv_obj_create(parent);
    if (!col)
        return nullptr;
    WidgetHelpers::resetContainerStyle(col);
    WidgetHelpers::disableInteract(col);
    lv_obj_set_size(col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(col, LayoutScale::y(topGapPx), LV_PART_MAIN);
    return col;
}

lv_obj_t *makeRow(lv_obj_t *parent, int16_t columnGapPx, lv_flex_align_t crossAlign) {
    lv_obj_t *row = lv_obj_create(parent);
    if (!row)
        return nullptr;
    WidgetHelpers::resetContainerStyle(row);
    WidgetHelpers::disableInteract(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, crossAlign, crossAlign);
    lv_obj_set_style_pad_column(row, LayoutScale::x(columnGapPx), LV_PART_MAIN);
    return row;
}

lv_obj_t *makeBar(lv_obj_t *parent, int16_t heightPx, uint32_t rgb) {
    lv_obj_t *bar = lv_obj_create(parent);
    if (!bar)
        return nullptr;
    WidgetHelpers::resetContainerStyle(bar);
    WidgetHelpers::disableInteract(bar);
    lv_obj_set_size(bar, LV_PCT(100), heightPx);
    lv_obj_set_style_bg_color(bar, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    return bar;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, int16_t trackingPx, uint32_t rgb) {
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return nullptr;
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, trackingPx, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    return label;
}

bool buildHead(Layer &layer) {
    layer.rule =
        makeBar(layer.root, LayoutScale::y(kRulePx), ThemeManager::getEffectiveTextColor());
    layer.kicker = makeLabel(layer.root, FontManager::label(kKickerFontPx), kKickerTrackingPx,
                             ThemeManager::dimColor());
    if (!layer.rule || !layer.kicker)
        return false;
    lv_obj_set_style_pad_top(layer.kicker, LayoutScale::y(kKickerGapPx), 0);
    return true;
}

bool buildHero(Layer &layer) {
    lv_obj_t *row = makeRow(layer.root, 0, LV_FLEX_ALIGN_END);
    if (!row)
        return false;
    lv_obj_set_style_pad_top(row, LayoutScale::y(kHeroGapPx), LV_PART_MAIN);
    layer.hero =
        makeLabel(row, FontManager::value(kHeroFontPx), 0, ThemeManager::getEffectiveTextColor());
    layer.heroUnit =
        makeLabel(row, FontManager::value(kHeroUnitFontPx), 0, ThemeManager::dimColor());
    return layer.hero && layer.heroUnit;
}

bool buildCells(Layer &layer) {
    layer.cells = makeRow(layer.bottom, kCellGapPx, LV_FLEX_ALIGN_START);
    if (!layer.cells)
        return false;
    lv_obj_set_style_pad_top(layer.cells, LayoutScale::y(kBottomGapPx), LV_PART_MAIN);
    for (uint8_t i = 0; i < ControlSplashContent::kMaxCells; ++i) {
        lv_obj_t *cell = makeColumn(layer.cells, 0);
        if (!cell)
            return false;
        lv_obj_set_width(cell, LV_SIZE_CONTENT);
        layer.cellKicker[i] = makeLabel(cell, FontManager::label(kKickerFontPx),
                                        kCellKickerTrackingPx, ThemeManager::dimColor());
        lv_obj_t *valueRow = makeRow(cell, 0, LV_FLEX_ALIGN_END);
        if (!valueRow)
            return false;
        lv_obj_set_width(valueRow, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_top(valueRow, LayoutScale::y(kCellKickerGapPx), LV_PART_MAIN);
        layer.cellValue[i] = makeLabel(valueRow, FontManager::value(kCellValueFontPx), 0,
                                       ThemeManager::getEffectiveTextColor());
        layer.cellUnit[i] = makeLabel(valueRow, FontManager::units(), 0, ThemeManager::dimColor());
        if (!layer.cellKicker[i] || !layer.cellValue[i] || !layer.cellUnit[i])
            return false;
        lv_obj_set_style_pad_bottom(
            layer.cellUnit[i],
            baselineDropPx(FontManager::value(kCellValueFontPx), FontManager::units()), 0);
    }
    return true;
}

bool buildBottom(Layer &layer) {
    layer.bottom = makeColumn(layer.root, 0);
    if (!layer.bottom)
        return false;
    if (!makeBar(layer.bottom, LayoutScale::y(kBottomRulePx), ThemeManager::trackColor()))
        return false;
    layer.line = makeLabel(layer.bottom, FontManager::units(), 0, ThemeManager::dimColor());
    if (!layer.line)
        return false;
    lv_obj_set_style_pad_top(layer.line, LayoutScale::y(kBottomGapPx), 0);
    return buildCells(layer);
}

bool buildSegments(Layer &layer) {
    layer.segments = makeRow(layer.root, kSegmentGapPx, LV_FLEX_ALIGN_START);
    if (!layer.segments)
        return false;
    lv_obj_set_height(layer.segments, LayoutScale::y(kSegmentHeightPx));
    for (uint8_t i = 0; i < CONTROL_STEP_MAX; ++i) {
        lv_obj_t *cell = makeBar(layer.segments, LV_PCT(100), ThemeManager::trackColor());
        if (!cell)
            return false;
        lv_obj_set_width(cell, 0);
        lv_obj_set_flex_grow(cell, 1);
        layer.segment[i] = cell;
    }
    return true;
}

lv_obj_t *makeSpacer(lv_obj_t *parent) {
    lv_obj_t *spacer = lv_obj_create(parent);
    if (!spacer)
        return nullptr;
    WidgetHelpers::resetContainerStyle(spacer);
    WidgetHelpers::disableInteract(spacer);
    lv_obj_set_size(spacer, LV_PCT(100), 0);
    lv_obj_set_flex_grow(spacer, 1);
    return spacer;
}

} // namespace

bool build(Layer &layer) {
    layer.root = OverlayScaffold::createRoot();
    if (!layer.root)
        return false;
    lv_obj_set_style_pad_all(layer.root, LayoutScale::x(kPadPx), LV_PART_MAIN);
    lv_obj_set_flex_flow(layer.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(layer.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(layer.root, 0, LV_PART_MAIN);
    if (!buildHead(layer) || !buildHero(layer))
        return false;
    layer.sub = makeLabel(layer.root, FontManager::label(kKickerFontPx), 0,
                          ThemeManager::getEffectiveTextColor());
    if (!layer.sub || !makeSpacer(layer.root))
        return false;
    return buildBottom(layer) && buildSegments(layer);
}

} // namespace ControlSplashInternal
