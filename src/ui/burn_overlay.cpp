#include "burn_overlay.h"
#include "app_config.h"
#include "ui/font_manager.h"
#include "ui/overlay_scaffold.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"
#include "layout_scale.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern SemaphoreHandle_t g_lvglMutex;

namespace {

lv_obj_t *s_overlay = nullptr;
lv_timer_t *s_errorTimer = nullptr;
lv_obj_t *s_pulseObj = nullptr;

constexpr uint8_t kKickerFontPx = 10;
constexpr uint8_t kStateFontPx = 14;
constexpr int16_t kPulseSquarePx = 8;
constexpr int16_t kRowGapPx = 12;

inline void assertUiThreadHoldsLvglMutex() {
    configASSERT(xPortGetCoreID() == TASK_CORE_UI);
    configASSERT(g_lvglMutex != nullptr);
    const TaskHandle_t holder = xSemaphoreGetMutexHolder(g_lvglMutex);
    configASSERT(holder == nullptr || holder == xTaskGetCurrentTaskHandle());
}

lv_obj_t *makeLine(lv_obj_t *parent, const char *text, uint32_t rgb, uint8_t fontPx) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_font(label, FontManager::label(fontPx), 0);
    return label;
}

void teardownOverlay() {
    if (s_errorTimer) {
        lv_timer_del(s_errorTimer);
        s_errorTimer = nullptr;
    }
    if (s_pulseObj) {
        OverlayScaffold::stopBreath(s_pulseObj);
        s_pulseObj = nullptr;
    }
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
}

const char *errorTitleFor(BurnOverlay::ErrorReason reason) {
    switch (reason) {
        case BurnOverlay::ErrorReason::WriteFailed:
            return "STORAGE WRITE FAILED";
    }
    return "SAVE FAILED";
}

const char *errorHintFor(BurnOverlay::ErrorReason reason) {
    switch (reason) {
        case BurnOverlay::ErrorReason::WriteFailed:
            return "RETRY FROM THE TUNER";
    }
    return "RETRY FROM THE TUNER";
}

void errorHoldExpiredCb(lv_timer_t *timer) {
    s_errorTimer = nullptr;
    (void)timer;
    BurnOverlay::hide();
}

} // namespace

void BurnOverlay::show() {
    assertUiThreadHoldsLvglMutex();

    teardownOverlay();

    lv_obj_t *root = OverlayScaffold::createRoot();
    lv_obj_t *col = OverlayScaffold::createCenterColumn(root, kRowGapPx);

    makeLine(col, "SAVING CONFIG", ThemeTokens::kDimNight, kKickerFontPx);
    makeLine(col, "WRITING TO STORAGE", ThemeTokens::kInkNight, kStateFontPx);

    s_pulseObj = WidgetHelpers::makeSquareBadge(col, LayoutScale::square(kPulseSquarePx),
                                                ThemeTokens::kEngaged);
    if (s_pulseObj) {
        OverlayScaffold::startBreath(s_pulseObj);
    }

    s_overlay = root;

    lv_refr_now(nullptr);
}

void BurnOverlay::hide() {
    if (!s_overlay && !s_errorTimer)
        return;
    teardownOverlay();
    lv_refr_now(nullptr);
}

void BurnOverlay::showError(ErrorReason reason) {
    teardownOverlay();

    lv_obj_t *root = OverlayScaffold::createRoot();
    lv_obj_t *col = OverlayScaffold::createCenterColumn(root, kRowGapPx);

    makeLine(col, "SAVING CONFIG", ThemeTokens::kDimNight, kKickerFontPx);
    makeLine(col, errorTitleFor(reason), ThemeTokens::kDanger, kStateFontPx);
    makeLine(col, errorHintFor(reason), ThemeTokens::kDimNight, kKickerFontPx);

    s_overlay = root;

    s_errorTimer = lv_timer_create(errorHoldExpiredCb, BURN_OVERLAY_ERROR_HOLD_MS, nullptr);
    if (s_errorTimer) {
        lv_timer_set_repeat_count(s_errorTimer, 1);
    }

    lv_refr_now(nullptr);
}
