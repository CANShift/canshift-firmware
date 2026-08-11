
#include "cruise_control_widget.h"

#include "ui/font_manager.h"
#include "ui/widget_factory.h"
#include "ui/widgets/cruise_l_shape.h"
#include "ui/widgets/widget_tag_pool.h"
#include "layout_scale.h"

#include "diag/logger.h"

#include <lvgl.h>
#include <string.h>

namespace CruiseControlWidget {

namespace {

constexpr int16_t CRUISE_BUTTON_W = 140;
constexpr int16_t CRUISE_BUTTON_H = 85;
constexpr int16_t CRUISE_GAP_X = 12;
constexpr int16_t CRUISE_GAP_Y = 10;
constexpr int16_t CRUISE_OUTER_PAD = 8;
constexpr int16_t CRUISE_CENTER_W = 100;
constexpr int16_t CRUISE_CENTER_H = 76;
constexpr int16_t CRUISE_NOTCH_MARGIN = 6;
constexpr int16_t CRUISE_NOTCH_W = CRUISE_CENTER_W / 2 + CRUISE_NOTCH_MARGIN;
constexpr int16_t CRUISE_NOTCH_H = CRUISE_CENTER_H / 2 + CRUISE_NOTCH_MARGIN;
constexpr int16_t CRUISE_BUTTON_RADIUS = 8;
constexpr int16_t CRUISE_INNER_R = 5;
constexpr int16_t CRUISE_BUTTON_BORDER_W = 2;
constexpr uint32_t CRUISE_BUTTON_FILL_RGB = 0x1A1A1Au;
constexpr uint32_t CRUISE_BUTTON_FILL_PRESSED_RGB = 0x3A3A3Au;
constexpr uint32_t CRUISE_BUTTON_STROKE_RGB = 0xE03030u;
constexpr uint32_t CRUISE_CENTER_DIM_RGB = 0x888888u;
constexpr uint32_t CRUISE_CENTER_VALUE_RGB = 0xFFFFFFu;
constexpr uint8_t CRUISE_LABEL_FONT_PX = 22;
constexpr int16_t CRUISE_LABEL_SYMBOL_HALF_W = 12;
constexpr int16_t CRUISE_LABEL_SYMBOL_HALF_H = 16;
constexpr int16_t CRUISE_LABEL_WORD_HALF_W = 24;
constexpr int16_t CRUISE_LABEL_WORD_HALF_H = 12;

struct CruiseToggleCtx {
    bool active;
    lv_obj_t *toggleBtn;
    lv_obj_t *toggleLabel;
};

struct CruiseButtonSpec {
    const char *id;
    const char *label;
    CfgCruiseOp op;
};

constexpr CruiseButtonSpec CRUISE_BUTTONS[4] = {
    {"cruise_minus", "-", CfgCruiseOp::DECREMENT},
    {"cruise_plus", "+", CfgCruiseOp::INCREMENT},
    {"cruise_set", "SET", CfgCruiseOp::SET},
    {"cruise_off", "OFF", CfgCruiseOp::OFF},
};

struct CruiseGrid {
    int16_t buttonW;
    int16_t buttonH;
    int16_t gapX;
    int16_t gapY;
    int16_t startX;
    int16_t startY;
};

CruiseLShape::Geometry cruiseGeometry() {
    return {LayoutScale::square(CRUISE_BUTTON_RADIUS), LayoutScale::square(CRUISE_INNER_R),
            LayoutScale::x(CRUISE_NOTCH_W), LayoutScale::y(CRUISE_NOTCH_H)};
}

CruiseLShape::Corner cornerFor(uint8_t index) {
    return static_cast<CruiseLShape::Corner>(index);
}

void cruiseLDrawCb(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_DRAW_MAIN_END)
        return;
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);
    if (!draw_ctx)
        return;
    const auto corner =
        static_cast<CruiseLShape::Corner>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    const CruiseLShape::Geometry geom = cruiseGeometry();

    lv_area_t area;
    lv_obj_get_coords(btn, &area);

    const lv_state_t state = lv_obj_get_state(btn);
    const bool active = (state & (LV_STATE_PRESSED | LV_STATE_CHECKED)) != 0;
    lv_draw_rect_dsc_t fill;
    lv_draw_rect_dsc_init(&fill);
    fill.bg_color = lv_color_hex(active ? CRUISE_BUTTON_FILL_PRESSED_RGB : CRUISE_BUTTON_FILL_RGB);
    fill.bg_opa = LV_OPA_COVER;
    fill.border_width = 0;
    fill.radius = 0;
    lv_area_t armH, armV;
    CruiseLShape::buildFillArms(area, corner, geom, &armH, &armV);
    lv_draw_rect(draw_ctx, &fill, &armH);
    lv_draw_rect(draw_ctx, &fill, &armV);

    lv_point_t pts[CruiseLShape::kMaxPoints];
    const uint8_t n = CruiseLShape::buildOutline(pts, area, corner, geom);
    lv_draw_line_dsc_t stroke;
    lv_draw_line_dsc_init(&stroke);
    stroke.color = lv_color_hex(CRUISE_BUTTON_STROKE_RGB);
    stroke.width = CRUISE_BUTTON_BORDER_W;
    stroke.opa = LV_OPA_COVER;
    stroke.round_start = 1;
    stroke.round_end = 1;
    for (uint8_t i = 0; i < n; ++i) {
        lv_draw_line(draw_ctx, &stroke, &pts[i], &pts[(i + 1u) % n]);
    }
}

void cruiseSyncToggleVisual(CruiseToggleCtx *ctx) {
    if (!ctx || !ctx->toggleBtn || !ctx->toggleLabel)
        return;
    if (ctx->active) {
        lv_obj_add_state(ctx->toggleBtn, LV_STATE_CHECKED);
        lv_label_set_text(ctx->toggleLabel, "ON");
    } else {
        lv_obj_clear_state(ctx->toggleBtn, LV_STATE_CHECKED);
        lv_label_set_text(ctx->toggleLabel, "OFF");
    }
    lv_obj_invalidate(ctx->toggleBtn);
}

void cruiseToggleClickCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    auto *ctx = static_cast<CruiseToggleCtx *>(lv_event_get_user_data(e));
    if (!ctx)
        return;
    ctx->active = !ctx->active;
    cruiseSyncToggleVisual(ctx);
}

void cruiseLHitTestCb(lv_event_t *e) {
    auto *info = lv_event_get_hit_test_info(e);
    if (!info || !info->point)
        return;
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const auto corner =
        static_cast<CruiseLShape::Corner>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    lv_area_t area;
    lv_obj_get_coords(btn, &area);
    if (CruiseLShape::hitInNotch(area, corner, cruiseGeometry(), *info->point)) {
        info->res = false;
    }
}

CfgWidget makeCruiseButton(const CruiseButtonSpec &spec, int16_t x, int16_t y,
                           CfgButtonParams *storage) {
    CfgWidget w = {};
    w.button = storage;
    *storage = CfgButtonParams{};
    strlcpy(w.id, spec.id, CFG_MAX_ID_LEN);
    w.type = WidgetType::BUTTON;
    w.signalId[0] = '\0';
    w.layout.x = x;
    w.layout.y = y;
    w.layout.w = CRUISE_BUTTON_W;
    w.layout.h = CRUISE_BUTTON_H;
    w.layout.zOrder = 0;

    w.style.primaryColor = CfgColor{CRUISE_BUTTON_FILL_RGB};
    w.style.textColor = CfgColor{CRUISE_CENTER_VALUE_RGB};
    w.style.fontSize = CRUISE_LABEL_FONT_PX;

    CfgButtonParams &p = *w.button;
    strlcpy(p.label, spec.label, CFG_MAX_NAME_LEN);
    p.isToggle = false;
    p.showLabel = true;
    p.hasColors = false;
    p.actionsCount = 1;

    CfgButtonAction &a = p.actions[0];
    a.type = CfgButtonActionType::CRUISE_CONTROL;
    a.pageId[0] = '\0';
    a.mapIndex = 0;
    a.canFrameId = 0;
    a.canDataLen = 0;
    a.canDataOffLen = 0;
    a.canExtended = false;
    a.cruiseOp = spec.op;
    a.cruiseStepKmh = 0;

    return w;
}

CruiseGrid computeCruiseGrid(int16_t contentY) {
    CruiseGrid g;
    g.buttonW = LayoutScale::x(CRUISE_BUTTON_W);
    g.buttonH = LayoutScale::y(CRUISE_BUTTON_H);
    g.gapX = LayoutScale::x(CRUISE_GAP_X);
    g.gapY = LayoutScale::y(CRUISE_GAP_Y);
    const int16_t outerPadX = LayoutScale::x(CRUISE_OUTER_PAD);
    const int16_t outerPadY = LayoutScale::y(CRUISE_OUTER_PAD);
    const int16_t gridW = g.buttonW * 2 + g.gapX;
    const int16_t gridH = g.buttonH * 2 + g.gapY;
    const int16_t contentH = LV_VER_RES - contentY;
    g.startX = (LV_HOR_RES - gridW) / 2;
    g.startY = contentY + (contentH - gridH) / 2;
    if (g.startX < outerPadX)
        g.startX = outerPadX;
    if (g.startY < contentY + outerPadY)
        g.startY = contentY + outerPadY;
    return g;
}

void initCruiseButtonShell(lv_obj_t *btn, int16_t x, int16_t y, const CruiseGrid &g) {
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, g.buttonW, g.buttonH);
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
        lv_obj_set_style_text_color(btn, lv_color_hex(CRUISE_CENTER_VALUE_RGB), LV_PART_MAIN | st);
    }
}

void attachCruiseLBehavior(lv_obj_t *btn, uint8_t cornerIdx) {
    void *cornerData = reinterpret_cast<void *>(static_cast<uintptr_t>(cornerIdx));
    lv_obj_add_event_cb(btn, cruiseLDrawCb, LV_EVENT_DRAW_MAIN_END, cornerData);

    auto pressInvalidateCb = [](lv_event_t *ev) { lv_obj_invalidate(lv_event_get_target(ev)); };
    lv_obj_add_event_cb(btn, pressInvalidateCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(btn, pressInvalidateCb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(btn, pressInvalidateCb, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_add_event_cb(btn, cruiseLHitTestCb, LV_EVENT_HIT_TEST, cornerData);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *makeCruiseFloatingLabel(lv_obj_t *btn, lv_obj_t *screen, uint8_t i, int16_t x, int16_t y,
                                  const CruiseGrid &g) {
    if (lv_obj_get_child_cnt(btn) > 0) {
        lv_obj_t *innerLabel = lv_obj_get_child(btn, 0);
        if (innerLabel) {
            lv_obj_add_flag(innerLabel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, CRUISE_BUTTONS[i].label);
    lv_obj_set_style_text_color(label, lv_color_hex(CRUISE_CENTER_VALUE_RGB), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, FontManager::value(CRUISE_LABEL_FONT_PX), 0);

    const uint8_t col = i % 2;
    const uint8_t row = i / 2;
    const int16_t offX = LayoutScale::x(CRUISE_NOTCH_W) / 2;
    const int16_t offY = LayoutScale::y(CRUISE_NOTCH_H) / 2;
    const int16_t cx = x + g.buttonW / 2 + (col == 0 ? -offX : offX);
    const int16_t cy = y + g.buttonH / 2 + (row == 0 ? -offY : offY);
    const bool isSymbol = i == static_cast<uint8_t>(CruiseLShape::Corner::kTL) ||
                          i == static_cast<uint8_t>(CruiseLShape::Corner::kTR);
    const int16_t halfW = isSymbol ? CRUISE_LABEL_SYMBOL_HALF_W : CRUISE_LABEL_WORD_HALF_W;
    const int16_t halfH = isSymbol ? CRUISE_LABEL_SYMBOL_HALF_H : CRUISE_LABEL_WORD_HALF_H;
    lv_obj_set_pos(label, cx - halfW, cy - halfH);
    return label;
}

void attachToggleCtx(lv_obj_t *btn, lv_obj_t *label, const CfgPage &cfg) {
    WidgetTagPool::Slot<CruiseToggleCtx> ctxSlot;
    CruiseToggleCtx *ctx = ctxSlot.get();
    if (!ctx) {
        LOG_WARN("UI", "Cruise toggle ctx pool exhausted on page '%s'", cfg.id);
        return;
    }
    ctx->active = false;
    ctx->toggleBtn = btn;
    ctx->toggleLabel = label;
    lv_obj_add_event_cb(btn, cruiseToggleClickCb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(btn, WidgetTagPool::deleteHandler<CruiseToggleCtx>, LV_EVENT_DELETE,
                        ctxSlot.commit());
}

bool buildCruiseCorner(lv_obj_t *screen, const CfgPage &cfg, const CruiseGrid &g, uint8_t i) {
    const uint8_t col = i % 2;
    const uint8_t row = i / 2;
    const int16_t x = g.startX + col * (g.buttonW + g.gapX);
    const int16_t y = g.startY + row * (g.buttonH + g.gapY);

    static CfgButtonParams s_cruiseParams[4];
    static CfgWidget s_cruiseWidgets[4];
    s_cruiseWidgets[i] = makeCruiseButton(CRUISE_BUTTONS[i], x, y, &s_cruiseParams[i]);
    lv_obj_t *btn = WidgetFactory::create(screen, s_cruiseWidgets[i], 0);
    if (!btn)
        return false;

    initCruiseButtonShell(btn, x, y, g);
    attachCruiseLBehavior(btn, i);
    lv_obj_t *label = makeCruiseFloatingLabel(btn, screen, i, x, y, g);

    if (cornerFor(i) == CruiseLShape::Corner::kBR) {
        attachToggleCtx(btn, label, cfg);
    }
    return true;
}

lv_obj_t *makeCenterLabel(lv_obj_t *center, const char *text, const lv_font_t *font, uint32_t rgb,
                          int16_t yPos, int16_t centerW) {
    lv_obj_t *label = lv_label_create(center);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_width(label, centerW);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 0, yPos);
    return label;
}

void buildCenterReadout(lv_obj_t *screen, int16_t contentY) {
    const int16_t contentH = LV_VER_RES - contentY;
    const int16_t centerW = LayoutScale::x(CRUISE_CENTER_W);
    const int16_t centerH = LayoutScale::y(CRUISE_CENTER_H);

    lv_obj_t *center = lv_obj_create(screen);
    lv_obj_set_pos(center, (LV_HOR_RES - centerW) / 2, contentY + (contentH - centerH) / 2);
    lv_obj_set_size(center, centerW, centerH);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_pad_all(center, 0, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_CLICKABLE);

    makeCenterLabel(center, "SET", FontManager::label(12), CRUISE_CENTER_DIM_RGB, LayoutScale::y(4),
                    centerW);
    makeCenterLabel(center, "0", FontManager::value(24), CRUISE_CENTER_VALUE_RGB,
                    (centerH - 24) / 2, centerW);
    makeCenterLabel(center, "km/h", FontManager::label(12), CRUISE_CENTER_DIM_RGB,
                    centerH - LayoutScale::y(16), centerW);
}

} // namespace

void build(lv_obj_t *screen, const CfgPage &cfg, int16_t contentY) {
    const CruiseGrid grid = computeCruiseGrid(contentY);

    uint8_t created = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        if (buildCruiseCorner(screen, cfg, grid, i)) {
            ++created;
        }
    }

    buildCenterReadout(screen, contentY);

    LOG_INFO("UI", "Built cruise_control template on page '%s' (%u/4 buttons + SET display)",
             cfg.id, created);
}

} // namespace CruiseControlWidget
