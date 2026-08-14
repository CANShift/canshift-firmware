#include "overlay_scaffold.h"

#include "layout_scale.h"

namespace OverlayScaffold {

namespace {

constexpr uint32_t kBreathHalfPeriodMs = 500;
constexpr lv_opa_t kPulseFloorOpa = 89;

constexpr int16_t kSleeveW = 18;
constexpr int16_t kSleeveH = 24;
constexpr int16_t kCableW = 4;
constexpr int16_t kCableH = 6;
constexpr int16_t kContactW = 10;
constexpr int16_t kContactH = 4;
constexpr int16_t kContactTopPad = 4;

void breathCb(void *obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
}

// Deliberately not WidgetHelpers::resetContainerStyle: this runs after the caller
// has set its own background, and that helper would reset bg_opa to transparent.
void flatten(lv_obj_t *obj) {
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *makeFilledRect(lv_obj_t *parent, int16_t w, int16_t h, uint32_t rgb, int16_t radius) {
    lv_obj_t *rect = lv_obj_create(parent);
    lv_obj_set_size(rect, LayoutScale::square(w), LayoutScale::square(h));
    lv_obj_set_style_radius(rect, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rect, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    flatten(rect);
    return rect;
}

} // namespace

lv_obj_t *createRoot(uint32_t backdropRgb) {
    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(backdropRgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(root, 0, LV_PART_MAIN);
    flatten(root);
    return root;
}

lv_obj_t *createCenterColumn(lv_obj_t *root, int16_t rowGapPx) {
    lv_obj_t *col = lv_obj_create(root);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
    flatten(col);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, rowGapPx, LV_PART_MAIN);
    return col;
}

lv_obj_t *makeUsbIcon(lv_obj_t *parent, uint32_t rgb) {
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, LayoutScale::square(kSleeveW), LayoutScale::square(kSleeveH + kCableH));
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    flatten(icon);

    lv_obj_t *sleeve = makeFilledRect(icon, kSleeveW, kSleeveH, rgb, 2);
    lv_obj_align(sleeve, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *contact = makeFilledRect(sleeve, kContactW, kContactH, kBackdropRgb, 1);
    lv_obj_align(contact, LV_ALIGN_TOP_MID, 0, LayoutScale::y(kContactTopPad));

    lv_obj_t *cable = makeFilledRect(icon, kCableW, kCableH, rgb, 1);
    lv_obj_align(cable, LV_ALIGN_BOTTOM_MID, 0, 0);

    return icon;
}

void startBreath(lv_obj_t *obj) {
    lv_anim_t breath;
    lv_anim_init(&breath);
    lv_anim_set_exec_cb(&breath, breathCb);
    lv_anim_set_var(&breath, obj);
    lv_anim_set_values(&breath, kPulseFloorOpa, LV_OPA_COVER);
    lv_anim_set_time(&breath, kBreathHalfPeriodMs);
    lv_anim_set_playback_time(&breath, kBreathHalfPeriodMs);
    lv_anim_set_repeat_count(&breath, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&breath);
}

void stopBreath(lv_obj_t *obj) {
    lv_anim_del(obj, breathCb);
}

} // namespace OverlayScaffold
