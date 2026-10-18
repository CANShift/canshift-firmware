
#include "cruise_control_widget.h"

#include "ui/font_manager.h"
#include "ui/widget_factory.h"
#include "ui/widgets/cruise_l_button.h"
#include "ui/widgets/widget_tag_pool.h"
#include "layout_scale.h"

#include "diag/logger.h"

#include <lvgl.h>
#include <string.h>

namespace CruiseControlWidget {

namespace {

constexpr int16_t kCruiseButtonW = 140;
constexpr int16_t kCruiseButtonH = 85;
constexpr int16_t kCruiseGapX = 12;
constexpr int16_t kCruiseGapY = 10;
constexpr int16_t kCruiseOuterPad = 8;
constexpr uint32_t kCruiseCenterDimRgb = 0x888888u;
constexpr uint8_t kCruiseLabelFontPx = 22;
constexpr int16_t kCruiseLabelSymbolHalfW = 12;
constexpr int16_t kCruiseLabelSymbolHalfH = 16;
constexpr int16_t kCruiseLabelWordHalfW = 24;
constexpr int16_t kCruiseLabelWordHalfH = 12;

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

constexpr CruiseButtonSpec kCruiseButtons[4] = {
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

CruiseLShape::Corner cornerFor(uint8_t index) {
    return static_cast<CruiseLShape::Corner>(index);
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

CfgWidget makeCruiseButton(const CruiseButtonSpec &spec, int16_t x, int16_t y,
                           CfgButtonParams *storage) {
    CfgWidget w = {};
    w.button = storage;
    *storage = CfgButtonParams{};
    strlcpy(w.id, spec.id, kCfgMaxIdLen);
    w.type = WidgetType::BUTTON;
    w.layout.x = x;
    w.layout.y = y;
    w.layout.w = kCruiseButtonW;
    w.layout.h = kCruiseButtonH;

    w.style.primaryColor = CfgColor{CruiseLButton::kFillRgb};
    w.style.textColor = CfgColor{CruiseLButton::kTextRgb};
    w.style.fontSize = kCruiseLabelFontPx;

    CfgButtonParams &p = *w.button;
    strlcpy(p.label, spec.label, kCfgMaxNameLen);
    p.showLabel = true;
    p.actionsCount = 1;

    CfgButtonAction &a = p.actions[0];
    a.type = CfgButtonActionType::CRUISE_CONTROL;
    a.cruiseOp = spec.op;

    return w;
}

CruiseGrid computeCruiseGrid(int16_t contentY) {
    CruiseGrid g;
    g.buttonW = LayoutScale::x(kCruiseButtonW);
    g.buttonH = LayoutScale::y(kCruiseButtonH);
    g.gapX = LayoutScale::x(kCruiseGapX);
    g.gapY = LayoutScale::y(kCruiseGapY);
    const int16_t outerPadX = LayoutScale::x(kCruiseOuterPad);
    const int16_t outerPadY = LayoutScale::y(kCruiseOuterPad);
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

lv_obj_t *makeCruiseFloatingLabel(lv_obj_t *btn, lv_obj_t *screen, uint8_t i, int16_t x, int16_t y,
                                  const CruiseGrid &g) {
    lv_obj_t *innerLabel = lv_obj_get_child(btn, 0);
    if (innerLabel)
        lv_obj_add_flag(innerLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, kCruiseButtons[i].label);
    lv_obj_set_style_text_color(label, lv_color_hex(CruiseLButton::kTextRgb), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, FontManager::value(kCruiseLabelFontPx), 0);

    const uint8_t col = i % 2;
    const uint8_t row = i / 2;
    const CruiseLShape::Geometry geom = CruiseLButton::geometry();
    const int16_t offX = geom.notchW / 2;
    const int16_t offY = geom.notchH / 2;
    const int16_t cx = x + g.buttonW / 2 + (col == 0 ? -offX : offX);
    const int16_t cy = y + g.buttonH / 2 + (row == 0 ? -offY : offY);
    const bool isSymbol = i == static_cast<uint8_t>(CruiseLShape::Corner::kTL) ||
                          i == static_cast<uint8_t>(CruiseLShape::Corner::kTR);
    const int16_t halfW = isSymbol ? kCruiseLabelSymbolHalfW : kCruiseLabelWordHalfW;
    const int16_t halfH = isSymbol ? kCruiseLabelSymbolHalfH : kCruiseLabelWordHalfH;
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
    s_cruiseWidgets[i] = makeCruiseButton(kCruiseButtons[i], x, y, &s_cruiseParams[i]);
    lv_obj_t *btn = WidgetFactory::create(screen, s_cruiseWidgets[i], 0);
    if (!btn)
        return false;

    CruiseLButton::initShell(btn, x, y, g.buttonW, g.buttonH);
    CruiseLButton::attach(btn, cornerFor(i));
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
    const int16_t centerW = LayoutScale::x(CruiseLButton::kReadoutW);
    const int16_t centerH = LayoutScale::y(CruiseLButton::kReadoutH);

    lv_obj_t *center = lv_obj_create(screen);
    lv_obj_set_pos(center, (LV_HOR_RES - centerW) / 2, contentY + (contentH - centerH) / 2);
    lv_obj_set_size(center, centerW, centerH);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_pad_all(center, 0, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_CLICKABLE);

    makeCenterLabel(center, "SET", FontManager::label(12), kCruiseCenterDimRgb, LayoutScale::y(4),
                    centerW);
    makeCenterLabel(center, "0", FontManager::value(24), CruiseLButton::kTextRgb,
                    (centerH - 24) / 2, centerW);
    makeCenterLabel(center, "km/h", FontManager::label(12), kCruiseCenterDimRgb,
                    centerH - LayoutScale::y(16), centerW);
}

}

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

}
