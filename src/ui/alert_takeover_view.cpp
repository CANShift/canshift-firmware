#include "alert_takeover_view.h"

#include "layout_scale.h"
#include "ui/font_manager.h"
#include "ui/theme_tokens.h"
#include "ui/widgets/widget_helpers.h"

namespace AlertTakeoverView {

namespace {

constexpr const char *kStopText = "STOP THE ENGINE";

constexpr int16_t kFieldPadPx = 8;
constexpr uint8_t kNameFontPx = 13;
constexpr int16_t kNameTrackingPx = 2;
constexpr uint8_t kValueFontPx = 84;
constexpr int16_t kValueTrackingPx = -4;
constexpr int16_t kValueGapPx = 4;
constexpr int16_t kContextGapPx = 3;
constexpr lv_opa_t kContextOpa = 204;
constexpr uint8_t kRulePx = 2;
constexpr lv_opa_t kRuleOpa = 128;
constexpr int16_t kCtaGapPx = 7;
constexpr uint8_t kCtaFontPx = 15;
constexpr int16_t kCtaTrackingPx = 2;

constexpr uint32_t kPulseHalfMs = 500;
constexpr lv_opa_t kPulseMinOpa = 89;

struct Field {
    lv_obj_t *root;
    lv_obj_t *ground;
    lv_obj_t *name;
    lv_obj_t *value;
    lv_obj_t *context;
    lv_obj_t *rule;
    lv_obj_t *stop;
};

Field s_field = {};
bool s_built = false;

lv_opa_t blend(int32_t opa, lv_opa_t ratio) {
    return static_cast<lv_opa_t>((opa * ratio) / LV_OPA_COVER);
}

void pulseCb(void *, int32_t opa) {
    if (!s_built)
        return;
    const lv_opa_t full = static_cast<lv_opa_t>(opa);
    lv_obj_set_style_bg_opa(s_field.ground, full, LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_field.name, full, 0);
    lv_obj_set_style_text_opa(s_field.value, full, 0);
    lv_obj_set_style_text_opa(s_field.stop, full, 0);
    lv_obj_set_style_text_opa(s_field.context, blend(opa, kContextOpa), 0);
    lv_obj_set_style_bg_opa(s_field.rule, blend(opa, kRuleOpa), LV_PART_MAIN);
}

void startPulse() {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_field.ground);
    lv_anim_set_exec_cb(&a, pulseCb);
    lv_anim_set_values(&a, LV_OPA_COVER, kPulseMinOpa);
    lv_anim_set_time(&a, kPulseHalfMs);
    lv_anim_set_playback_time(&a, kPulseHalfMs);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, int16_t trackingPx, int16_t padTopPx) {
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return nullptr;
    lv_label_set_text(label, "");
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(ThemeTokens::kInkNight), 0);
    lv_obj_set_style_text_letter_space(label, trackingPx, 0);
    lv_obj_set_style_pad_top(label, padTopPx, 0);
    return label;
}

void buildRoot(lv_event_cb_t ackCb) {
    s_field.root = lv_obj_create(lv_layer_top());
    if (!s_field.root)
        return;
    WidgetHelpers::resetContainerStyle(s_field.root);
    lv_obj_set_size(s_field.root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_field.root, 0, 0);
    lv_obj_set_style_bg_color(s_field.root, lv_color_hex(ThemeTokens::kGroundNight), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_field.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(s_field.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_field.root, ackCb, LV_EVENT_CLICKED, nullptr);
}

void buildGround() {
    s_field.ground = lv_obj_create(s_field.root);
    if (!s_field.ground)
        return;
    WidgetHelpers::resetContainerStyle(s_field.ground);
    lv_obj_set_size(s_field.ground, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_field.ground, LayoutScale::square(kFieldPadPx), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_field.ground, lv_color_hex(ThemeTokens::kDanger), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_field.ground, LV_OPA_COVER, LV_PART_MAIN);
}

void buildReading() {
    lv_obj_t *col = WidgetHelpers::makeFlushColumn(s_field.ground);
    if (!col)
        return;
    WidgetHelpers::disableInteract(col);
    lv_obj_align(col, LV_ALIGN_TOP_LEFT, 0, 0);
    s_field.name = makeLabel(col, FontManager::label(kNameFontPx), kNameTrackingPx, 0);
    s_field.value = makeLabel(col, FontManager::value(kValueFontPx), kValueTrackingPx,
                              LayoutScale::y(kValueGapPx));
    s_field.context = makeLabel(col, FontManager::units(), 0, LayoutScale::y(kContextGapPx));
    if (s_field.context)
        lv_obj_set_style_text_opa(s_field.context, kContextOpa, 0);
}

void buildCallToAction() {
    lv_obj_t *col = WidgetHelpers::makeFlushColumn(s_field.ground);
    if (!col)
        return;
    WidgetHelpers::disableInteract(col);
    s_field.rule = WidgetHelpers::makeTopRule(col, kRulePx, ThemeTokens::kInkNight);
    if (s_field.rule)
        lv_obj_set_style_bg_opa(s_field.rule, kRuleOpa, LV_PART_MAIN);
    s_field.stop =
        makeLabel(col, FontManager::label(kCtaFontPx), kCtaTrackingPx, LayoutScale::y(kCtaGapPx));
    if (s_field.stop)
        lv_label_set_text(s_field.stop, kStopText);
    lv_obj_align(col, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

bool allPartsPresent() {
    return s_field.name && s_field.value && s_field.context && s_field.rule && s_field.stop;
}

} // namespace

void build(lv_event_cb_t ackCb) {
    buildRoot(ackCb);
    if (!s_field.root)
        return;
    buildGround();
    if (!s_field.ground)
        return;
    buildReading();
    buildCallToAction();
    s_built = allPartsPresent();
    setHidden(true);
    if (s_built)
        startPulse();
}

bool isBuilt() {
    return s_built;
}

void setHidden(bool hidden) {
    WidgetHelpers::setVisible(s_field.root, !hidden);
}

void setSignalName(const char *text) {
    WidgetHelpers::setLabelTextIfChanged(s_field.name, text);
}

void setValue(const char *text) {
    WidgetHelpers::setLabelTextIfChanged(s_field.value, text);
}

void setContext(const char *text) {
    WidgetHelpers::setLabelTextIfChanged(s_field.context, text);
}

} // namespace AlertTakeoverView
