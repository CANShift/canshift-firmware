#include "ota_overlay.h"
#include "ui/font_manager.h"
#include "ui/overlay_scaffold.h"
#include "layout_scale.h"

#include <atomic>
#include <lvgl.h>
#include <stdio.h>

namespace {

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_arc = nullptr;
lv_obj_t *s_progressBar = nullptr;
lv_obj_t *s_subLabel = nullptr;
lv_obj_t *s_breathObj = nullptr;
lv_timer_t *s_arcTimer = nullptr;

std::atomic<size_t> s_total{0};
std::atomic<size_t> s_written{0};
size_t s_lastRenderedWritten = SIZE_MAX;

uint16_t s_arcAngle = 0;
constexpr uint32_t kUsbIconRgb = 0xFF3030;

void arcTimerCb(lv_timer_t *timer) {
    OverlayScaffold::stepSpinner(static_cast<lv_obj_t *>(timer->user_data), s_arcAngle);
}

void teardownOverlay() {
    if (s_arcTimer) {
        lv_timer_del(s_arcTimer);
        s_arcTimer = nullptr;
    }
    if (s_breathObj) {
        OverlayScaffold::stopBreath(s_breathObj);
        s_breathObj = nullptr;
    }
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
    s_arc = nullptr;
    s_progressBar = nullptr;
    s_subLabel = nullptr;
    s_lastRenderedWritten = SIZE_MAX;
}

} // namespace

namespace OtaOverlay {

void show(size_t totalBytes) {
    teardownOverlay();

    s_total.store(totalBytes, std::memory_order_relaxed);
    s_written.store(0, std::memory_order_relaxed);
    s_lastRenderedWritten = SIZE_MAX;

    lv_obj_t *root = OverlayScaffold::createRoot();
    lv_obj_t *col = OverlayScaffold::createCenterColumn(root, 12);

    lv_obj_t *arc = OverlayScaffold::makeSpinnerArc(col, 56);
    s_arcAngle = 0;
    s_arcTimer = lv_timer_create(arcTimerCb, OverlayScaffold::kSpinnerTickMs, arc);

    lv_obj_t *title = lv_label_create(col);
    lv_label_set_text(title, "Updating firmware");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);

    lv_obj_t *bar = lv_bar_create(col);
    lv_obj_set_size(bar, 200, 6);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);

    lv_obj_t *sub = lv_label_create(col);
    lv_label_set_text(sub, "0 / 0 KB");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(sub, FontManager::label(12), 0);

    s_breathObj = OverlayScaffold::makeUsbIcon(col, kUsbIconRgb);
    OverlayScaffold::startBreath(s_breathObj);

    s_overlay = root;
    s_arc = arc;
    s_progressBar = bar;
    s_subLabel = sub;

    lv_refr_now(nullptr);
}

void setProgress(size_t writtenBytes) {
    s_written.store(writtenBytes, std::memory_order_relaxed);
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
    if (s_overlay == nullptr)
        return;
    const size_t written = s_written.load(std::memory_order_relaxed);
    if (written == s_lastRenderedWritten)
        return;
    s_lastRenderedWritten = written;
    const size_t total = s_total.load(std::memory_order_relaxed);
    if (s_progressBar && total > 0) {
        const int32_t pctMille = static_cast<int32_t>((written * 1000ULL) / total);
        lv_bar_set_value(s_progressBar, pctMille, LV_ANIM_OFF);
    }
    if (s_subLabel) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%u / %u KB", static_cast<unsigned>(written / 1024),
                 static_cast<unsigned>(s_total.load(std::memory_order_relaxed) / 1024));
        lv_label_set_text(s_subLabel, buf);
    }
}

} // namespace Detail

} // namespace OtaOverlay
