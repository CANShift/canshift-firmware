#include "ota_overlay.h"
#include "ui/font_manager.h"
#include "ui/overlay_scaffold.h"
#include "ui/theme_tokens.h"
#include "app_config.h"
#include "layout_scale.h"

#include <atomic>
#include <lvgl.h>
#include <stdio.h>

namespace {

constexpr uint32_t kGroundRgb = ThemeTokens::kGroundNight;
constexpr uint32_t kFailedGroundRgb = ThemeTokens::kDanger;
constexpr uint32_t kInkRgb = ThemeTokens::kInkNight;
constexpr uint32_t kDimRgb = ThemeTokens::kDimNight;
constexpr uint32_t kTrackRgb = ThemeTokens::kTrackNight;
constexpr uint32_t kAccentRgb = ThemeTokens::kEngaged;
constexpr uint32_t kWarnRgb = ThemeTokens::kWarn;

constexpr int16_t kFramePadPx = 8;
constexpr int16_t kBarHeightPx = 10;
constexpr int16_t kKickerGapPx = 10;
constexpr int16_t kFootGapPx = 8;

constexpr uint8_t kKickerFontPx = 12;
constexpr uint8_t kFootFontPx = 12;
constexpr uint8_t kValueFontPx = 48;
constexpr uint8_t kUnitFontPx = 17;
constexpr uint8_t kCodeFontPx = 32;

constexpr int16_t kKickerTrackingPx = 2;

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_progressFill = nullptr;
lv_obj_t *s_percentLabel = nullptr;

std::atomic<size_t> s_total{0};
std::atomic<size_t> s_written{0};
size_t s_lastRenderedWritten = SIZE_MAX;
int16_t s_barMaxW = 0;

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

void teardownOverlay() {
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
    s_progressFill = nullptr;
    s_percentLabel = nullptr;
    s_lastRenderedWritten = SIZE_MAX;
    s_barMaxW = 0;
}

void dismissCb(lv_event_t *) {
    OtaOverlay::hide();
}

lv_obj_t *makeKicker(lv_obj_t *root, const char *text, uint32_t rgb) {
    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_font(label, FontManager::label(kKickerFontPx), 0);
    lv_obj_set_style_text_letter_space(label, kKickerTrackingPx, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, LayoutScale::x(kFramePadPx),
                 LayoutScale::y(kFramePadPx));
    return label;
}

lv_obj_t *makeValueRow(lv_obj_t *root, lv_obj_t *kicker) {
    lv_obj_t *row = lv_obj_create(root);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, LayoutScale::x(4), LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_align_to(row, kicker, LV_ALIGN_OUT_BOTTOM_LEFT, 0, LayoutScale::y(kKickerGapPx));
    return row;
}

lv_obj_t *makeValue(lv_obj_t *row, const char *text, uint8_t sizePx, uint32_t rgb) {
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_font(label, FontManager::value(sizePx), 0);
    return label;
}

lv_obj_t *makeFoot(lv_obj_t *root, const char *text, uint32_t rgb) {
    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_font(label, FontManager::label(kFootFontPx), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_LEFT, LayoutScale::x(kFramePadPx),
                 -LayoutScale::y(kFramePadPx));
    return label;
}

lv_obj_t *makeRect(lv_obj_t *parent, int16_t w, int16_t h, uint32_t rgb) {
    lv_obj_t *rect = lv_obj_create(parent);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_style_bg_color(rect, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(rect, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rect, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return rect;
}

void buildProgressBar(lv_obj_t *root, lv_obj_t *foot) {
    const int16_t width = static_cast<int16_t>(LV_HOR_RES - 2 * LayoutScale::x(kFramePadPx));
    const int16_t height = LayoutScale::y(kBarHeightPx);
    lv_obj_t *track = makeRect(root, width, height, kTrackRgb);
    lv_obj_align_to(track, foot, LV_ALIGN_OUT_TOP_LEFT, 0, -LayoutScale::y(kFootGapPx));
    s_progressFill = makeRect(track, 1, height, kAccentRgb);
    lv_obj_align(s_progressFill, LV_ALIGN_LEFT_MID, 0, 0);
    s_barMaxW = width;
}

void renderPercent(size_t written, size_t total) {
    const uint8_t pct =
        total > 0 ? static_cast<uint8_t>((written * 100ULL) / total) : static_cast<uint8_t>(0);
    if (s_percentLabel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(pct));
        lv_label_set_text(s_percentLabel, buf);
    }
    if (s_progressFill && s_barMaxW > 0) {
        const int16_t fillW = static_cast<int16_t>((s_barMaxW * pct) / 100);
        lv_obj_set_width(s_progressFill, fillW > 0 ? fillW : 1);
    }
}

} // namespace

namespace OtaOverlay {

void show(size_t totalBytes) {
    teardownOverlay();

    s_total.store(totalBytes, std::memory_order_relaxed);
    s_written.store(0, std::memory_order_relaxed);

    lv_obj_t *root = OverlayScaffold::createRoot(kGroundRgb);
    lv_obj_t *kicker = makeKicker(root, "UPDATING FIRMWARE", kDimRgb);

    lv_obj_t *row = makeValueRow(root, kicker);
    s_percentLabel = makeValue(row, "0", kValueFontPx, kInkRgb);
    makeValue(row, "%", kUnitFontPx, kDimRgb);

    lv_obj_t *foot = makeFoot(root, "DO NOT UNPLUG", kWarnRgb);
    buildProgressBar(root, foot);

    s_overlay = root;
    renderPercent(0, totalBytes);
    lv_refr_now(nullptr);
}

void setProgress(size_t writtenBytes) {
    s_written.store(writtenBytes, std::memory_order_relaxed);
}

void showComplete() {
    teardownOverlay();

    lv_obj_t *root = OverlayScaffold::createRoot(kGroundRgb);
    lv_obj_t *kicker = makeKicker(root, "UPDATE COMPLETE", kDimRgb);
    makeValue(makeValueRow(root, kicker), APP_VERSION_STR, kCodeFontPx, kInkRgb);
    makeFoot(root, "RESTARTING", kDimRgb);

    s_overlay = root;
    lv_refr_now(nullptr);
}

void showFailed(FailReason reason, uint32_t detailCode) {
    teardownOverlay();

    lv_obj_t *root = OverlayScaffold::createRoot(kFailedGroundRgb);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, dismissCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *kicker = makeKicker(root, failKicker(reason), kInkRgb);

    char code[16];
    snprintf(code, sizeof(code), "0x%X", static_cast<unsigned>(detailCode));
    makeValue(makeValueRow(root, kicker), code, kCodeFontPx, kInkRgb);

    char kept[40];
    snprintf(kept, sizeof(kept), "KEPT %s", APP_VERSION_STR);
    makeFoot(root, kept, kInkRgb);

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
    renderPercent(written, s_total.load(std::memory_order_relaxed));
}

} // namespace Detail

} // namespace OtaOverlay
