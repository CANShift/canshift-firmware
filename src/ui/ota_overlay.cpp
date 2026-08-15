#include "ota_overlay.h"
#include "ui/font_manager.h"
#include "ui/overlay_scaffold.h"
#include "ui/severity.h"
#include "ui/theme_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"
#include "app_config.h"
#include "layout_scale.h"

#include <atomic>
#include <lvgl.h>
#include <stdio.h>

namespace {

constexpr int16_t kFramePadPx = 8;
constexpr int16_t kPercentGapPx = 22;
constexpr int16_t kVersionGapPx = 7;
constexpr int16_t kBarHeightPx = 5;
constexpr int16_t kUnplugGapPx = 6;

constexpr uint8_t kPercentFontPx = 48;
constexpr uint8_t kUnplugFontPx = 10;
constexpr uint8_t kCodeFontPx = 32;

constexpr int16_t kPercentTrackingPx = -2;
constexpr int16_t kUnplugTrackingPx = 1;

constexpr size_t kVersionLineLen = 40;

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_progressFill = nullptr;
lv_obj_t *s_percentLabel = nullptr;

std::atomic<size_t> s_total{0};
std::atomic<size_t> s_written{0};
size_t s_lastRenderedWritten = SIZE_MAX;
int16_t s_barMaxW = 0;
int16_t s_barFillW = 0;

struct FailText {
    OtaOverlay::FailReason reason;
    const char *kicker;
};

constexpr FailText kFailTexts[] = {{OtaOverlay::FailReason::Write, "WRITE FAILED"},
                                   {OtaOverlay::FailReason::Commit, "UPDATE FAILED"},
                                   {OtaOverlay::FailReason::Aborted, "UPDATE ABORTED"}};

const char *failKicker(OtaOverlay::FailReason reason) {
    for (const FailText &entry : kFailTexts) {
        if (entry.reason == reason)
            return entry.kicker;
    }
    return "UPDATE FAILED";
}

uint32_t groundRgb() {
    return ThemeManager::pickColor(ThemeTokens::kGroundNight, ThemeTokens::kGroundDay);
}

int16_t contentWidthPx() {
    return static_cast<int16_t>(LV_HOR_RES - 2 * LayoutScale::x(kFramePadPx));
}

void teardownOverlay() {
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
    s_progressFill = nullptr;
    s_percentLabel = nullptr;
    s_lastRenderedWritten = SIZE_MAX;
    s_barMaxW = 0;
    s_barFillW = 0;
}

void dismissCb(lv_event_t *) {
    OtaOverlay::hide();
}

lv_obj_t *makeBand(lv_obj_t *root, lv_align_t align, int16_t yOffsetPx) {
    lv_obj_t *band = lv_obj_create(root);
    WidgetHelpers::resetContainerStyle(band);
    lv_obj_set_size(band, contentWidthPx(), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(band, 0, LV_PART_MAIN);
    lv_obj_align(band, align, LayoutScale::x(kFramePadPx), LayoutScale::y(yOffsetPx));
    return band;
}

lv_obj_t *makeMono(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t rgb) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    return label;
}

lv_obj_t *makeRect(lv_obj_t *parent, int16_t w, int16_t h, uint32_t rgb) {
    lv_obj_t *rect = lv_obj_create(parent);
    WidgetHelpers::resetContainerStyle(rect);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_style_bg_color(rect, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    return rect;
}

lv_obj_t *buildHead(lv_obj_t *root, const char *kicker, Severity::Level level) {
    lv_obj_t *band = makeBand(root, LV_ALIGN_TOP_LEFT, kFramePadPx);
    const Severity::Spec spec = {level, kicker, Severity::kRulePrimaryPx};
    const Severity::Surface surface = Severity::build(band, spec);
    return surface.value;
}

void buildPercent(lv_obj_t *body) {
    lv_obj_t *row = lv_obj_create(body);
    WidgetHelpers::resetContainerStyle(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 0, LV_PART_MAIN);
    s_percentLabel = makeMono(row, "0", FontManager::value(kPercentFontPx),
                              ThemeManager::getEffectiveTextColor());
    lv_obj_set_style_text_letter_space(s_percentLabel, kPercentTrackingPx, 0);
    makeMono(row, " %", FontManager::units(), ThemeManager::dimColor());
}

void formatVersionLine(char *out, size_t outLen, const char *targetVersion) {
    if (targetVersion == nullptr || targetVersion[0] == '\0') {
        snprintf(out, outLen, "%s", APP_VERSION_STR);
        return;
    }
    snprintf(out, outLen, "%s → %s", APP_VERSION_STR, targetVersion);
}

void buildVersionLine(lv_obj_t *body, const char *targetVersion) {
    char line[kVersionLineLen];
    formatVersionLine(line, sizeof(line), targetVersion);
    lv_obj_t *label = makeMono(body, line, FontManager::units(), ThemeManager::dimColor());
    lv_obj_set_style_pad_top(label, LayoutScale::y(kVersionGapPx), 0);
}

lv_obj_t *makeFootLabel(lv_obj_t *band, const char *text, uint32_t rgb, int16_t padTopPx) {
    lv_obj_t *label = lv_label_create(band);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, FontManager::label(kUnplugFontPx), 0);
    lv_obj_set_style_text_letter_space(label, kUnplugTrackingPx, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_pad_top(label, padTopPx, 0);
    return label;
}

void buildProgressFoot(lv_obj_t *root) {
    lv_obj_t *band = makeBand(root, LV_ALIGN_BOTTOM_LEFT, -kFramePadPx);
    const int16_t height = LayoutScale::y(kBarHeightPx);
    lv_obj_t *track = makeRect(band, contentWidthPx(), height, ThemeManager::trackColor());
    s_progressFill = makeRect(track, 0, height, ThemeTokens::kEngaged);
    lv_obj_align(s_progressFill, LV_ALIGN_LEFT_MID, 0, 0);
    s_barMaxW = contentWidthPx();
    makeFootLabel(band, "DO NOT UNPLUG", ThemeManager::warnColor(), LayoutScale::y(kUnplugGapPx));
}

void buildFootLine(lv_obj_t *root, const char *text, uint32_t rgb) {
    lv_obj_t *band = makeBand(root, LV_ALIGN_BOTTOM_LEFT, -kFramePadPx);
    makeFootLabel(band, text, rgb, 0);
}

void setFillWidth(void *obj, int32_t width) {
    lv_obj_set_width(static_cast<lv_obj_t *>(obj), static_cast<int16_t>(width));
}

uint8_t percentOf(size_t written, size_t total) {
    if (total == 0)
        return 0;
    return static_cast<uint8_t>((static_cast<uint64_t>(written) * 100ULL) / total);
}

void renderPercent(size_t written, size_t total, bool animate) {
    const uint8_t pct = percentOf(written, total);
    if (s_percentLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(pct));
        lv_label_set_text(s_percentLabel, buf);
    }
    if (s_progressFill == nullptr || s_barMaxW <= 0)
        return;
    const int16_t target = static_cast<int16_t>((s_barMaxW * pct) / 100);
    if (animate) {
        WidgetHelpers::animateFill(s_progressFill, setFillWidth, s_barFillW, target);
    } else {
        WidgetHelpers::setFillImmediate(s_progressFill, setFillWidth, target);
    }
    s_barFillW = target;
}

} // namespace

namespace OtaOverlay {

void show(size_t totalBytes, const char *targetVersion) {
    teardownOverlay();

    s_total.store(totalBytes, std::memory_order_relaxed);
    s_written.store(0, std::memory_order_relaxed);

    lv_obj_t *root = OverlayScaffold::createRoot(groundRgb());
    lv_obj_t *body = buildHead(root, "UPDATING FIRMWARE", Severity::Level::INFORMATION);
    lv_obj_set_style_pad_top(body, LayoutScale::y(kPercentGapPx), LV_PART_MAIN);
    buildPercent(body);
    buildVersionLine(body, targetVersion);
    buildProgressFoot(root);

    s_overlay = root;
    renderPercent(0, totalBytes, false);
    lv_refr_now(nullptr);
}

void setProgress(size_t writtenBytes) {
    s_written.store(writtenBytes, std::memory_order_relaxed);
}

void showComplete() {
    teardownOverlay();

    lv_obj_t *root = OverlayScaffold::createRoot(groundRgb());
    lv_obj_t *body = buildHead(root, "UPDATE COMPLETE", Severity::Level::INFORMATION);
    makeMono(body, APP_VERSION_STR, FontManager::value(kCodeFontPx),
             ThemeManager::getEffectiveTextColor());
    buildFootLine(root, "RESTARTING", ThemeManager::dimColor());

    s_overlay = root;
    lv_refr_now(nullptr);
}

void showFailed(FailReason reason, uint32_t detailCode) {
    teardownOverlay();

    lv_obj_t *root = OverlayScaffold::createRoot(groundRgb());
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, dismissCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *body = buildHead(root, failKicker(reason), Severity::Level::FAILURE);

    char code[16];
    snprintf(code, sizeof(code), "0x%X", static_cast<unsigned>(detailCode));
    makeMono(body, code, FontManager::value(kCodeFontPx), ThemeManager::dangerColor());

    char kept[kVersionLineLen];
    snprintf(kept, sizeof(kept), "KEPT %s", APP_VERSION_STR);
    buildFootLine(root, kept, ThemeManager::dimColor());

    s_overlay = root;
    lv_refr_now(nullptr);
}

void hide() {
    teardownOverlay();
    s_total.store(0, std::memory_order_relaxed);
    s_written.store(0, std::memory_order_relaxed);
}

bool isActive() {
    return s_overlay != nullptr;
}

namespace Detail {

void tick() {
    if (s_overlay == nullptr || s_percentLabel == nullptr)
        return;
    const size_t written = s_written.load(std::memory_order_relaxed);
    if (written == s_lastRenderedWritten)
        return;
    s_lastRenderedWritten = written;
    renderPercent(written, s_total.load(std::memory_order_relaxed), true);
}

} // namespace Detail

} // namespace OtaOverlay
