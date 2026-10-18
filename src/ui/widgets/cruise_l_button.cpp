#include "cruise_l_button.h"

#include "layout_scale.h"

#include <initializer_list>

namespace CruiseLButton {

namespace {

constexpr int16_t kNotchMargin = 6;
constexpr int16_t kNotchW = kReadoutW / 2 + kNotchMargin;
constexpr int16_t kNotchH = kReadoutH / 2 + kNotchMargin;
constexpr int16_t kRadius = 8;
constexpr int16_t kInnerR = 5;
constexpr int16_t kBorderW = 2;
constexpr uint32_t kFillPressedRgb = 0x3A3A3Au;
constexpr uint32_t kStrokeRgb = 0xE03030u;

CruiseLShape::Corner cornerOf(lv_event_t *e) {
    return static_cast<CruiseLShape::Corner>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
}

void drawFill(lv_draw_ctx_t *drawCtx, lv_obj_t *btn, const lv_area_t &area,
              CruiseLShape::Corner corner, const CruiseLShape::Geometry &geom) {
    const bool active = (lv_obj_get_state(btn) & (LV_STATE_PRESSED | LV_STATE_CHECKED)) != 0;
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_hex(active ? kFillPressedRgb : kFillRgb);
    fill.bg_opa = LV_OPA_COVER;
    fill.border_width = 0;
    fill.radius = 0;
    lv_area_t armH, armV;
    CruiseLShape::buildFillArms(area, corner, geom, &armH, &armV);
    lv_draw_rect(drawCtx, &fill, &armH);
    lv_draw_rect(drawCtx, &fill, &armV);
}

void drawOutline(lv_draw_ctx_t *drawCtx, const lv_area_t &area, CruiseLShape::Corner corner,
                 const CruiseLShape::Geometry &geom) {
    lv_point_t pts[CruiseLShape::kMaxPoints];
    const uint8_t n = CruiseLShape::buildOutline(pts, area, corner, geom);
    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.color = lv_color_hex(kStrokeRgb);
    stroke.width = kBorderW;
    stroke.opa = LV_OPA_COVER;
    stroke.round_start = 1;
    stroke.round_end = 1;
    for (uint8_t i = 0; i < n; ++i)
        lv_draw_line(drawCtx, &stroke, &pts[i], &pts[(i + 1u) % n]);
}

void drawCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN_END)
        return;
    lv_draw_ctx_t *drawCtx = lv_event_get_draw_ctx(e);
    if (!drawCtx)
        return;
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const CruiseLShape::Corner corner = cornerOf(e);
    const CruiseLShape::Geometry geom = geometry();
    lv_area_t area;
    lv_obj_get_coords(btn, &area);
    drawFill(drawCtx, btn, area, corner, geom);
    drawOutline(drawCtx, area, corner, geom);
}

void hitTestCb(lv_event_t *e) {
    auto *info = lv_event_get_hit_test_info(e);
    if (!info || !info->point)
        return;
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_area_t area;
    lv_obj_get_coords(btn, &area);
    if (CruiseLShape::hitInNotch(area, cornerOf(e), geometry(), *info->point))
        info->res = false;
}

void invalidateCb(lv_event_t *e) {
    lv_obj_invalidate(lv_event_get_target(e));
}

}

CruiseLShape::Geometry geometry() {
    return {LayoutScale::square(kRadius), LayoutScale::square(kInnerR), LayoutScale::x(kNotchW),
            LayoutScale::y(kNotchH)};
}

void initShell(lv_obj_t *btn, int16_t x, int16_t y, int16_t w, int16_t h) {
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    for (lv_state_t st :
         {lv_state_t(LV_STATE_DEFAULT), lv_state_t(LV_STATE_PRESSED), lv_state_t(LV_STATE_FOCUSED),
          lv_state_t(LV_STATE_FOCUS_KEY), lv_state_t(LV_STATE_CHECKED), lv_state_t(LV_STATE_EDITED),
          lv_state_t(LV_STATE_HOVERED), lv_state_t(LV_STATE_DISABLED)}) {
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
        lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
        lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | st);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
        lv_obj_set_style_text_color(btn, lv_color_hex(kTextRgb), LV_PART_MAIN | st);
    }
}

void attach(lv_obj_t *btn, CruiseLShape::Corner corner) {
    void *cornerData = reinterpret_cast<void *>(static_cast<uintptr_t>(corner));
    lv_obj_add_event_cb(btn, drawCb, LV_EVENT_DRAW_MAIN_END, cornerData);
    lv_obj_add_event_cb(btn, invalidateCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(btn, invalidateCb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(btn, invalidateCb, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(btn, hitTestCb, LV_EVENT_HIT_TEST, cornerData);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
}

}
