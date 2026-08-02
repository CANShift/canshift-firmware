#include "setup_screen.h"

#include "app_config.h"
#include "diag/logger.h"
#include "ui/brand_mark.h"
#include "ui/font_manager.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>
#include <stdio.h>

namespace SetupScreen {

namespace {

constexpr uint32_t kBgRgb = 0x121212;
constexpr uint32_t kInkRgb = 0xFFFFFF;
constexpr uint32_t kAccentRgb = 0xFF4747;
constexpr uint32_t kDimRgb = 0xBABABA;

constexpr int16_t kMarginPx = 17;
constexpr int16_t kWaitSquarePx = 7;

void animBreath(void *obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
}

lv_obj_t *makeMonoLabel(lv_obj_t *parent, const char *text, uint32_t rgb, uint8_t fontSize) {
    lv_obj_t *lbl = lv_label_create(parent);
    if (!lbl)
        return nullptr;
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_font(lbl, FontManager::label(fontSize), 0);
    return lbl;
}

} // namespace

void show() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(scr, lv_color_hex(kBgRgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = BrandMark::create(scr, false);
    if (header)
        lv_obj_align(header, LV_ALIGN_TOP_LEFT, kMarginPx, 22);

    char verBuf[24];
    snprintf(verBuf, sizeof(verBuf), "fw " APP_VERSION_STR);
    lv_obj_t *ver = makeMonoLabel(scr, verBuf, kDimRgb, 10);
    if (ver)
        lv_obj_align(ver, LV_ALIGN_TOP_RIGHT, -kMarginPx, 34);

    lv_obj_t *title = makeMonoLabel(scr, "READY TO CONFIGURE", kInkRgb, 16);
    if (title)
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, kMarginPx, 92);

    lv_obj_t *intro = makeMonoLabel(scr, "No dashboard config on this device.", kDimRgb, 10);
    if (intro)
        lv_obj_align(intro, LV_ALIGN_TOP_LEFT, kMarginPx, 120);

    lv_obj_t *urlIntro = makeMonoLabel(scr, "Open the CANShift Tuner at", kDimRgb, 10);
    if (urlIntro)
        lv_obj_align(urlIntro, LV_ALIGN_TOP_LEFT, kMarginPx, 142);

    lv_obj_t *url = makeMonoLabel(scr, "canshift-tuner.vercel.app", kInkRgb, 10);
    if (url)
        lv_obj_align(url, LV_ALIGN_TOP_LEFT, kMarginPx, 158);

    lv_obj_t *how = makeMonoLabel(scr, "in a Chromium browser and connect over USB.", kDimRgb, 10);
    if (how)
        lv_obj_align(how, LV_ALIGN_TOP_LEFT, kMarginPx, 174);

    lv_obj_t *waitSquare = WidgetHelpers::makeSquareBadge(scr, kWaitSquarePx, kAccentRgb);
    if (waitSquare)
        lv_obj_align(waitSquare, LV_ALIGN_BOTTOM_LEFT, kMarginPx, -19);

    lv_obj_t *waiting = makeMonoLabel(scr, "WAITING FOR USB", kAccentRgb, 10);
    if (waiting)
        lv_obj_align(waiting, LV_ALIGN_BOTTOM_LEFT, kMarginPx + kWaitSquarePx + 6, -17);

    if (waitSquare) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_exec_cb(&a, animBreath);
        lv_anim_set_var(&a, waitSquare);
        lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
        lv_anim_set_time(&a, 900);
        lv_anim_set_playback_time(&a, 900);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }

    lv_scr_load(scr);
    LOG_INFO("UI", "Setup screen shown — waiting for Tuner connection");
}

} // namespace SetupScreen
