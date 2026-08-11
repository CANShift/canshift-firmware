#include "burn_overlay.h"
#include "app_config.h"
#include "ui/font_manager.h"
#include "ui/overlay_scaffold.h"
#include "layout_scale.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern SemaphoreHandle_t g_lvglMutex;

namespace {

lv_obj_t *s_overlay = nullptr;
lv_obj_t *s_arc = nullptr;
lv_timer_t *s_errorTimer = nullptr;
lv_timer_t *s_arcTimer = nullptr;
lv_obj_t *s_breathAnimObj = nullptr;
uint16_t s_arcAngle = 0;
constexpr uint32_t kUsbIconRgb = 0xFF3030;

inline void assertUiThreadHoldsLvglMutex() {
    configASSERT(xPortGetCoreID() == TASK_CORE_UI);
    configASSERT(g_lvglMutex != nullptr);
    const TaskHandle_t holder = xSemaphoreGetMutexHolder(g_lvglMutex);
    configASSERT(holder == nullptr || holder == xTaskGetCurrentTaskHandle());
}

void arcTimerCb(lv_timer_t *timer) {
    OverlayScaffold::stepSpinner(static_cast<lv_obj_t *>(timer->user_data), s_arcAngle);
}

void teardownOverlay() {
    if (s_errorTimer) {
        lv_timer_del(s_errorTimer);
        s_errorTimer = nullptr;
    }
    if (s_arcTimer) {
        lv_timer_del(s_arcTimer);
        s_arcTimer = nullptr;
    }
    if (s_breathAnimObj) {
        OverlayScaffold::stopBreath(s_breathAnimObj);
        s_breathAnimObj = nullptr;
    }
    s_arc = nullptr;
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
}

const char *errorTitleFor(BurnOverlay::ErrorReason reason) {
    switch (reason) {
        case BurnOverlay::ErrorReason::WriteFailed:
            return "Storage write failed";
    }
    return "Save failed";
}

const char *errorHintFor(BurnOverlay::ErrorReason reason) {
    switch (reason) {
        case BurnOverlay::ErrorReason::WriteFailed:
            return "Retry from studio";
    }
    return "Retry from studio";
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

    lv_obj_t *col = OverlayScaffold::createCenterColumn(root, 14);
    lv_obj_t *arc = OverlayScaffold::makeSpinnerArc(col, 64);

    s_arcAngle = 0;
    s_arcTimer = lv_timer_create(arcTimerCb, OverlayScaffold::kSpinnerTickMs, arc);

    lv_obj_t *title = lv_label_create(col);
    lv_label_set_text(title, "Saving config...");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);

    lv_obj_t *sub = lv_label_create(col);
    lv_label_set_text(sub, "Writing to storage...");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(sub, FontManager::label(12), 0);

    s_breathAnimObj = OverlayScaffold::makeUsbIcon(col, kUsbIconRgb);
    OverlayScaffold::startBreath(s_breathAnimObj);

    s_overlay = root;
    s_arc = arc;

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

    lv_obj_t *icon = lv_label_create(root);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xE04040), 0);
    lv_obj_set_style_text_font(icon, FontManager::value(24), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -32);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, errorTitleFor(reason));
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, FontManager::label(16), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 12);

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, errorHintFor(reason));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(hint, FontManager::label(12), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 40);

    s_overlay = root;

    s_errorTimer = lv_timer_create(errorHoldExpiredCb, BURN_OVERLAY_ERROR_HOLD_MS, nullptr);
    if (s_errorTimer) {
        lv_timer_set_repeat_count(s_errorTimer, 1);
    }

    lv_refr_now(nullptr);
}
