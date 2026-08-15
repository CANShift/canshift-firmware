#include "cut_band.h"

#include "dash_metrics.h"
#include "layout_scale.h"
#include "page_manager.h"
#include "ui/cut_sources.h"
#include "ui/font_manager.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widget_styles.h"
#include "ui/widgets/widget_helpers.h"
#include "diag/logger.h"
#include "util/format_float.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

namespace {

constexpr int16_t kRowHeightPx = 26;
constexpr int16_t kColumnGapPx = DashMetrics::kRowGapPx;
constexpr int16_t kNameTrackingPx = 1;
constexpr size_t kDetailBufLen = 64;
constexpr size_t kReadoutBufLen = 12;
constexpr size_t kRowCapacity = ALERT_CUT_ROW_CAPACITY;
constexpr uint32_t kRgbUnset = 0xFFFFFFFFu;

constexpr const char *kFixedReadout[] = {nullptr, "HOLDING", "LATCHED"};
constexpr size_t kReadoutCount = sizeof(kFixedReadout) / sizeof(kFixedReadout[0]);

struct BandRow {
    lv_obj_t *root;
    lv_obj_t *rule;
    lv_obj_t *name;
    lv_obj_t *detail;
    lv_obj_t *elapsed;
    Severity::Surface severity;
    uint32_t lastDetailRgb;
    uint32_t lastElapsedRgb;
    SignalId offending;
    Severity::Level level;
};

lv_obj_t *s_container = nullptr;
BandRow s_rows[kRowCapacity];
uint8_t s_visibleCount = 0;
CutBandStateRs s_state;
int16_t s_alignedY = -1;

uint32_t groundRgb() {
    const CfgColor ground = {
        ThemeManager::pickColor(ThemeTokens::kGroundNight, ThemeTokens::kGroundDay)};
    return ThemeManager::getEffectiveBgColor(ground).rgb;
}

uint32_t detailRgbFor(Severity::Level level) {
    if (level == Severity::Level::FAILURE)
        return ThemeManager::getEffectiveTextColor();
    return ThemeManager::dimColor();
}

lv_obj_t *makeStrip(lv_obj_t *parent, lv_coord_t heightPx) {
    lv_obj_t *strip = lv_obj_create(parent);
    if (!strip)
        return nullptr;
    WidgetHelpers::resetContainerStyle(strip);
    WidgetHelpers::disableInteract(strip);
    lv_obj_set_size(strip, LV_PCT(100), heightPx);
    return strip;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, int16_t trackingPx) {
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return nullptr;
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, trackingPx, 0);
    return label;
}

lv_obj_t *makeLine(lv_obj_t *root) {
    lv_obj_t *line = makeStrip(root, LV_SIZE_CONTENT);
    if (!line)
        return nullptr;
    lv_obj_set_style_pad_top(line, LayoutScale::y(Severity::kRuleGapPx), LV_PART_MAIN);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(line, LayoutScale::x(kColumnGapPx), LV_PART_MAIN);
    return line;
}

void applyRowTheme(BandRow &row) {
    if (!row.root)
        return;
    row.severity = Severity::adopt(row.rule, row.name, nullptr, Severity::kRulePrimaryPx,
                                   ThemeManager::getEffectiveTextColor());
    Severity::repaint(row.severity, row.level);
    row.lastDetailRgb = kRgbUnset;
    row.lastElapsedRgb = kRgbUnset;
    WidgetStyles::setTextColorIfChanged(row.detail, row.lastDetailRgb, detailRgbFor(row.level));
    WidgetStyles::setTextColorIfChanged(row.elapsed, row.lastElapsedRgb, ThemeManager::dimColor());
}

bool buildRowParts(BandRow &row, lv_obj_t *parent) {
    row.root = makeStrip(parent, LayoutScale::y(kRowHeightPx));
    if (!row.root)
        return false;
    lv_obj_set_flex_flow(row.root, LV_FLEX_FLOW_COLUMN);
    row.rule = makeStrip(row.root, Severity::kRulePrimaryPx);
    lv_obj_t *line = makeLine(row.root);
    if (!row.rule || !line)
        return false;
    lv_obj_set_style_bg_opa(row.rule, LV_OPA_COVER, LV_PART_MAIN);
    row.name = makeLabel(line, FontManager::label(Severity::kKickerPx), kNameTrackingPx);
    row.detail = makeLabel(line, FontManager::units(), 0);
    row.elapsed = makeLabel(line, FontManager::units(), 0);
    return row.name != nullptr && row.detail != nullptr && row.elapsed != nullptr;
}

void buildRow(BandRow &row, lv_obj_t *parent) {
    if (!buildRowParts(row, parent)) {
        LOG_ERROR("UI", "Cut band row alloc failed — LVGL pool OOM");
        if (row.root)
            lv_obj_del(row.root);
        row.root = nullptr;
        return;
    }
    lv_obj_set_flex_grow(row.detail, 1);
    lv_label_set_long_mode(row.detail, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_pad_left(row.elapsed, LayoutScale::x(kColumnGapPx), LV_PART_MAIN);
    row.offending = SignalIds::SIGNAL_COUNT;
    row.level = Severity::Level::INFORMATION;
    applyRowTheme(row);
    WidgetHelpers::setVisible(row.root, false);
}

lv_obj_t *createContainer() {
    lv_obj_t *container = lv_obj_create(lv_layer_top());
    if (!container)
        return nullptr;
    WidgetHelpers::resetContainerStyle(container);
    WidgetHelpers::disableInteract(container);
    const int16_t inset = LayoutScale::x(DashMetrics::kFramePaddingPx);
    lv_obj_set_size(container, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(container, lv_color_hex(groundRgb()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_left(container, inset, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, inset, LV_PART_MAIN);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(container, 0, LV_PART_MAIN);
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    return container;
}

void alignToContentTop() {
    const int16_t y = PageManager::currentContentTopY();
    if (y == s_alignedY)
        return;
    s_alignedY = y;
    lv_obj_align(s_container, LV_ALIGN_TOP_LEFT, 0, y);
}

void formatReadout(const CutRowRs &data, char *out, size_t len) {
    const size_t idx = data.readout < kReadoutCount ? data.readout : 0;
    if (kFixedReadout[idx] != nullptr) {
        strlcpy(out, kFixedReadout[idx], len);
        return;
    }
    const size_t used =
        FloatFormat::formatFixed(out, len, static_cast<float>(data.elapsed_ms) / 1000.0f, 1);
    if (used >= len)
        return;
    strlcpy(out + used, " s", len - used);
}

void renderRow(BandRow &row, const CutRowRs &data, const SignalStore::SignalValue *snap) {
    if (!row.root)
        return;
    row.level = Severity::fromRaw(data.severity);
    row.offending = CutSources::offendingSignal(data.kind);
    Severity::repaint(row.severity, row.level);
    WidgetHelpers::setLabelTextIfChanged(row.name, cut_kind_name_rs(data.kind));

    char detail[kDetailBufLen];
    CutSources::composeDetail(data.kind, snap, detail, sizeof(detail));
    WidgetHelpers::setLabelTextIfChanged(row.detail, detail);
    WidgetStyles::setTextColorIfChanged(row.detail, row.lastDetailRgb, detailRgbFor(row.level));

    char readout[kReadoutBufLen];
    formatReadout(data, readout, sizeof(readout));
    WidgetHelpers::setLabelTextIfChanged(row.elapsed, readout);
    WidgetHelpers::setVisibleIfChanged(row.root, true);
}

void hideRow(BandRow &row) {
    row.offending = SignalIds::SIGNAL_COUNT;
    WidgetHelpers::setVisibleIfChanged(row.root, false);
}

} // namespace

void CutBand::init() {
    cut_band_reset_rs(&s_state);
    s_visibleCount = 0;
    s_alignedY = -1;
    s_container = createContainer();
    if (!s_container)
        return;
    for (BandRow &row : s_rows) {
        buildRow(row, s_container);
    }
}

void CutBand::reapplyTheme() {
    if (!s_container)
        return;
    lv_obj_set_style_bg_color(s_container, lv_color_hex(groundRgb()), LV_PART_MAIN);
    for (BandRow &row : s_rows) {
        applyRowTheme(row);
    }
}

void CutBand::update(const SignalStore::SignalValue *snap) {
    if (!s_container || snap == nullptr)
        return;

    const uint32_t now = millis();
    cut_band_step_rs(&s_state, CutSources::activeFlags(snap), now);

    CutRowRs rows[kRowCapacity];
    s_visibleCount = cut_band_rows_rs(&s_state, now, rows);

    for (size_t i = 0; i < kRowCapacity; ++i) {
        if (i < s_visibleCount) {
            renderRow(s_rows[i], rows[i], snap);
            continue;
        }
        hideRow(s_rows[i]);
    }

    alignToContentTop();
    WidgetHelpers::setVisibleIfChanged(s_container, s_visibleCount > 0);
}

Severity::Level CutBand::levelFor(SignalId signal) {
    if (signal >= SignalIds::SIGNAL_COUNT)
        return Severity::Level::INFORMATION;
    for (size_t i = 0; i < s_visibleCount; ++i) {
        if (s_rows[i].offending == signal)
            return s_rows[i].level;
    }
    return Severity::Level::INFORMATION;
}
