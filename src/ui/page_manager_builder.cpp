
#include "page_manager_internal.h"

#include "font_manager.h"
#include "icon_assets.h"
#include "theme_manager.h"
#include "top_bar.h"
#include "widget_factory.h"

#include "config/config_loader.h"
#include "diag/logger.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>

namespace PageManagerInternal {

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
constexpr uint8_t CRUISE_BEZIER_SEGS = 4;
constexpr uint8_t CRUISE_L_MAX_PTS = 40;
constexpr uint32_t CRUISE_BUTTON_FILL_RGB = 0x1A1A1Au;
constexpr uint32_t CRUISE_BUTTON_FILL_PRESSED_RGB = 0x3A3A3Au;
constexpr uint32_t CRUISE_BUTTON_STROKE_RGB = 0xE03030u;
constexpr uint32_t CRUISE_CENTER_DIM_RGB = 0x888888u;
constexpr uint32_t CRUISE_CENTER_VALUE_RGB = 0xFFFFFFu;

enum class CruiseCorner : uint8_t { kTL = 0, kTR = 1, kBL = 2, kBR = 3 };

static bool s_cruiseActive = false;
static lv_obj_t *s_cruiseToggleBtn = nullptr;
static lv_obj_t *s_cruiseToggleLabel = nullptr;

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

inline lv_point_t cruiseBezierAt(int16_t x0, int16_t y0, int16_t xc, int16_t yc, int16_t x2,
                                 int16_t y2, float t) {
    const float omt = 1.0f - t;
    return {static_cast<lv_coord_t>(lroundf(omt * omt * x0 + 2.0f * omt * t * xc + t * t * x2)),
            static_cast<lv_coord_t>(lroundf(omt * omt * y0 + 2.0f * omt * t * yc + t * t * y2))};
}

inline void cruiseEmitBezier(lv_point_t *out, uint8_t &n, int16_t x0, int16_t y0, int16_t xc,
                             int16_t yc, int16_t x2, int16_t y2) {

    for (uint8_t i = 1; i <= CRUISE_BEZIER_SEGS; ++i) {
        out[n++] =
            cruiseBezierAt(x0, y0, xc, yc, x2, y2, static_cast<float>(i) / CRUISE_BEZIER_SEGS);
    }
}

uint8_t buildCruiseLPath(lv_point_t *pts, const lv_area_t &area, CruiseCorner corner) {
    const int16_t x = area.x1;
    const int16_t y = area.y1;
    const int16_t w = static_cast<int16_t>(area.x2 - area.x1 + 1);
    const int16_t h = static_cast<int16_t>(area.y2 - area.y1 + 1);
    const int16_t r = CRUISE_BUTTON_RADIUS;
    const int16_t ir = CRUISE_INNER_R;
    const int16_t nW = CRUISE_NOTCH_W;
    const int16_t nH = CRUISE_NOTCH_H;
    uint8_t n = 0;

    switch (corner) {
        case CruiseCorner::kTL:

            pts[n++] = {static_cast<lv_coord_t>(x + r), y};
            pts[n++] = {static_cast<lv_coord_t>(x + w - r), y};
            cruiseEmitBezier(pts, n, x + w - r, y, x + w, y, x + w, y + r);
            pts[n++] = {static_cast<lv_coord_t>(x + w), static_cast<lv_coord_t>(y + h - nH - ir)};
            cruiseEmitBezier(pts, n, x + w, y + h - nH - ir, x + w, y + h - nH, x + w - ir,
                             y + h - nH);
            pts[n++] = {static_cast<lv_coord_t>(x + w - nW + ir),
                        static_cast<lv_coord_t>(y + h - nH)};
            cruiseEmitBezier(pts, n, x + w - nW + ir, y + h - nH, x + w - nW, y + h - nH,
                             x + w - nW, y + h - nH + ir);
            pts[n++] = {static_cast<lv_coord_t>(x + w - nW), static_cast<lv_coord_t>(y + h - ir)};
            cruiseEmitBezier(pts, n, x + w - nW, y + h - ir, x + w - nW, y + h, x + w - nW - ir,
                             y + h);
            pts[n++] = {static_cast<lv_coord_t>(x + r), static_cast<lv_coord_t>(y + h)};
            cruiseEmitBezier(pts, n, x + r, y + h, x, y + h, x, y + h - r);
            pts[n++] = {x, static_cast<lv_coord_t>(y + r)};
            cruiseEmitBezier(pts, n, x, y + r, x, y, x + r, y);
            break;
        case CruiseCorner::kTR:

            pts[n++] = {static_cast<lv_coord_t>(x + r), y};
            pts[n++] = {static_cast<lv_coord_t>(x + w - r), y};
            cruiseEmitBezier(pts, n, x + w - r, y, x + w, y, x + w, y + r);
            pts[n++] = {static_cast<lv_coord_t>(x + w), static_cast<lv_coord_t>(y + h - r)};
            cruiseEmitBezier(pts, n, x + w, y + h - r, x + w, y + h, x + w - r, y + h);
            pts[n++] = {static_cast<lv_coord_t>(x + nW + ir), static_cast<lv_coord_t>(y + h)};
            cruiseEmitBezier(pts, n, x + nW + ir, y + h, x + nW, y + h, x + nW, y + h - ir);
            pts[n++] = {static_cast<lv_coord_t>(x + nW), static_cast<lv_coord_t>(y + h - nH + ir)};
            cruiseEmitBezier(pts, n, x + nW, y + h - nH + ir, x + nW, y + h - nH, x + nW - ir,
                             y + h - nH);
            pts[n++] = {static_cast<lv_coord_t>(x + ir), static_cast<lv_coord_t>(y + h - nH)};
            cruiseEmitBezier(pts, n, x + ir, y + h - nH, x, y + h - nH, x, y + h - nH - ir);
            pts[n++] = {x, static_cast<lv_coord_t>(y + r)};
            cruiseEmitBezier(pts, n, x, y + r, x, y, x + r, y);
            break;
        case CruiseCorner::kBL:

            pts[n++] = {static_cast<lv_coord_t>(x + r), y};
            pts[n++] = {static_cast<lv_coord_t>(x + w - nW - ir), y};
            cruiseEmitBezier(pts, n, x + w - nW - ir, y, x + w - nW, y, x + w - nW, y + ir);
            pts[n++] = {static_cast<lv_coord_t>(x + w - nW), static_cast<lv_coord_t>(y + nH - ir)};
            cruiseEmitBezier(pts, n, x + w - nW, y + nH - ir, x + w - nW, y + nH, x + w - nW + ir,
                             y + nH);
            pts[n++] = {static_cast<lv_coord_t>(x + w - ir), static_cast<lv_coord_t>(y + nH)};
            cruiseEmitBezier(pts, n, x + w - ir, y + nH, x + w, y + nH, x + w, y + nH + ir);
            pts[n++] = {static_cast<lv_coord_t>(x + w), static_cast<lv_coord_t>(y + h - r)};
            cruiseEmitBezier(pts, n, x + w, y + h - r, x + w, y + h, x + w - r, y + h);
            pts[n++] = {static_cast<lv_coord_t>(x + r), static_cast<lv_coord_t>(y + h)};
            cruiseEmitBezier(pts, n, x + r, y + h, x, y + h, x, y + h - r);
            pts[n++] = {x, static_cast<lv_coord_t>(y + r)};
            cruiseEmitBezier(pts, n, x, y + r, x, y, x + r, y);
            break;
        case CruiseCorner::kBR:

            pts[n++] = {static_cast<lv_coord_t>(x + nW + ir), y};
            pts[n++] = {static_cast<lv_coord_t>(x + w - r), y};
            cruiseEmitBezier(pts, n, x + w - r, y, x + w, y, x + w, y + r);
            pts[n++] = {static_cast<lv_coord_t>(x + w), static_cast<lv_coord_t>(y + h - r)};
            cruiseEmitBezier(pts, n, x + w, y + h - r, x + w, y + h, x + w - r, y + h);
            pts[n++] = {static_cast<lv_coord_t>(x + r), static_cast<lv_coord_t>(y + h)};
            cruiseEmitBezier(pts, n, x + r, y + h, x, y + h, x, y + h - r);
            pts[n++] = {x, static_cast<lv_coord_t>(y + nH + ir)};
            cruiseEmitBezier(pts, n, x, y + nH + ir, x, y + nH, x + ir, y + nH);
            pts[n++] = {static_cast<lv_coord_t>(x + nW - ir), static_cast<lv_coord_t>(y + nH)};
            cruiseEmitBezier(pts, n, x + nW - ir, y + nH, x + nW, y + nH, x + nW, y + nH - ir);
            pts[n++] = {static_cast<lv_coord_t>(x + nW), static_cast<lv_coord_t>(y + ir)};
            cruiseEmitBezier(pts, n, x + nW, y + ir, x + nW, y, x + nW + ir, y);
            break;
    }
    return n;
}

void buildCruiseLFillArms(const lv_area_t &btn, CruiseCorner corner, lv_area_t *armH,
                          lv_area_t *armV) {
    const int16_t x = btn.x1;
    const int16_t y = btn.y1;
    const int16_t w = static_cast<int16_t>(btn.x2 - btn.x1 + 1);
    const int16_t h = static_cast<int16_t>(btn.y2 - btn.y1 + 1);
    const int16_t nW = CRUISE_NOTCH_W;
    const int16_t nH = CRUISE_NOTCH_H;
    switch (corner) {
        case CruiseCorner::kTL:

            *armH = {x, y, static_cast<lv_coord_t>(x + w - 1),
                     static_cast<lv_coord_t>(y + h - nH - 1)};
            *armV = {x, y, static_cast<lv_coord_t>(x + w - nW - 1),
                     static_cast<lv_coord_t>(y + h - 1)};
            break;
        case CruiseCorner::kTR:
            *armH = {x, y, static_cast<lv_coord_t>(x + w - 1),
                     static_cast<lv_coord_t>(y + h - nH - 1)};
            *armV = {static_cast<lv_coord_t>(x + nW), y, static_cast<lv_coord_t>(x + w - 1),
                     static_cast<lv_coord_t>(y + h - 1)};
            break;
        case CruiseCorner::kBL:
            *armH = {x, static_cast<lv_coord_t>(y + nH), static_cast<lv_coord_t>(x + w - 1),
                     static_cast<lv_coord_t>(y + h - 1)};
            *armV = {x, y, static_cast<lv_coord_t>(x + w - nW - 1),
                     static_cast<lv_coord_t>(y + h - 1)};
            break;
        case CruiseCorner::kBR:
            *armH = {x, static_cast<lv_coord_t>(y + nH), static_cast<lv_coord_t>(x + w - 1),
                     static_cast<lv_coord_t>(y + h - 1)};
            *armV = {static_cast<lv_coord_t>(x + nW), y, static_cast<lv_coord_t>(x + w - 1),
                     static_cast<lv_coord_t>(y + h - 1)};
            break;
    }
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
        static_cast<CruiseCorner>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));

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
    buildCruiseLFillArms(area, corner, &armH, &armV);
    lv_draw_rect(draw_ctx, &fill, &armH);
    lv_draw_rect(draw_ctx, &fill, &armV);

    lv_point_t pts[CRUISE_L_MAX_PTS];
    const uint8_t n = buildCruiseLPath(pts, area, corner);
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

void cruiseSyncToggleVisual() {
    if (!s_cruiseToggleBtn || !s_cruiseToggleLabel)
        return;
    if (s_cruiseActive) {
        lv_obj_add_state(s_cruiseToggleBtn, LV_STATE_CHECKED);
        lv_label_set_text(s_cruiseToggleLabel, "ON");
    } else {
        lv_obj_clear_state(s_cruiseToggleBtn, LV_STATE_CHECKED);
        lv_label_set_text(s_cruiseToggleLabel, "OFF");
    }
    lv_obj_invalidate(s_cruiseToggleBtn);
}

void cruiseToggleClickCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    s_cruiseActive = !s_cruiseActive;
    cruiseSyncToggleVisual();
}

void cruiseLHitTestCb(lv_event_t *e) {
    auto *info = lv_event_get_hit_test_info(e);
    if (!info || !info->point)
        return;
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const auto corner =
        static_cast<CruiseCorner>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    lv_area_t area;
    lv_obj_get_coords(btn, &area);
    const int16_t px = info->point->x - area.x1;
    const int16_t py = info->point->y - area.y1;
    const int16_t w = static_cast<int16_t>(area.x2 - area.x1 + 1);
    const int16_t h = static_cast<int16_t>(area.y2 - area.y1 + 1);
    const int16_t nW = CRUISE_NOTCH_W;
    const int16_t nH = CRUISE_NOTCH_H;
    bool inNotch = false;
    switch (corner) {
        case CruiseCorner::kTL:
            inNotch = (px >= w - nW) && (py >= h - nH);
            break;
        case CruiseCorner::kTR:
            inNotch = (px < nW) && (py >= h - nH);
            break;
        case CruiseCorner::kBL:
            inNotch = (px >= w - nW) && (py < nH);
            break;
        case CruiseCorner::kBR:
            inNotch = (px < nW) && (py < nH);
            break;
    }
    if (inNotch)
        info->res = false;
}

CfgWidget makeCruiseButton(const CruiseButtonSpec &spec, const CfgPage &pageCfg, int16_t x,
                           int16_t y) {
    CfgWidget w = {};
    strlcpy(w.id, spec.id, CFG_MAX_ID_LEN);
    w.type = WidgetType::BUTTON;
    w.signalId[0] = '\0';
    w.layout.x = x;
    w.layout.y = y;
    w.layout.w = CRUISE_BUTTON_W;
    w.layout.h = CRUISE_BUTTON_H;
    w.layout.zOrder = 0;

    (void)pageCfg;
    w.style.primaryColor = CfgColor{CRUISE_BUTTON_FILL_RGB};
    w.style.textColor = CfgColor{0xFFFFFFu};
    w.style.fontSize = 22;

    CfgButtonParams &p = w.button;
    strlcpy(p.label, spec.label, CFG_MAX_NAME_LEN);
    p.iconPath[0] = '\0';
    p.iconName[0] = '\0';
    p.isToggle = false;
    p.showIcon = false;
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

void buildCruiseControlTemplate(lv_obj_t *screen, const CfgPage &cfg, int16_t contentY) {
    const int16_t gridW = CRUISE_BUTTON_W * 2 + CRUISE_GAP_X;
    const int16_t gridH = CRUISE_BUTTON_H * 2 + CRUISE_GAP_Y;
    const int16_t contentH = LV_VER_RES - contentY;
    int16_t startX = (LV_HOR_RES - gridW) / 2;
    int16_t startY = contentY + (contentH - gridH) / 2;
    if (startX < CRUISE_OUTER_PAD)
        startX = CRUISE_OUTER_PAD;
    if (startY < contentY + CRUISE_OUTER_PAD)
        startY = contentY + CRUISE_OUTER_PAD;

    s_cruiseActive = false;
    s_cruiseToggleBtn = nullptr;
    s_cruiseToggleLabel = nullptr;

    uint8_t created = 0;
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t col = i % 2;
        const uint8_t row = i / 2;
        const int16_t x = startX + col * (CRUISE_BUTTON_W + CRUISE_GAP_X);
        const int16_t y = startY + row * (CRUISE_BUTTON_H + CRUISE_GAP_Y);
        const CfgWidget w = makeCruiseButton(CRUISE_BUTTONS[i], cfg, x, y);
        lv_obj_t *btn = WidgetFactory::create(screen, w, 0);
        if (!btn)
            continue;

        lv_obj_remove_style_all(btn);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, CRUISE_BUTTON_W, CRUISE_BUTTON_H);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        for (lv_state_t st : {lv_state_t(LV_STATE_DEFAULT), lv_state_t(LV_STATE_PRESSED),
                              lv_state_t(LV_STATE_FOCUSED), lv_state_t(LV_STATE_FOCUS_KEY),
                              lv_state_t(LV_STATE_CHECKED), lv_state_t(LV_STATE_EDITED),
                              lv_state_t(LV_STATE_HOVERED), lv_state_t(LV_STATE_DISABLED)}) {
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
            lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
            lv_obj_set_style_outline_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
            lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | st);
            lv_obj_set_style_shadow_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | st);
            lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFFu), LV_PART_MAIN | st);
        }

        void *cornerData = reinterpret_cast<void *>(static_cast<uintptr_t>(i));
        lv_obj_add_event_cb(btn, cruiseLDrawCb, LV_EVENT_DRAW_MAIN_END, cornerData);

        auto pressInvalidateCb = [](lv_event_t *ev) { lv_obj_invalidate(lv_event_get_target(ev)); };
        lv_obj_add_event_cb(btn, pressInvalidateCb, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(btn, pressInvalidateCb, LV_EVENT_RELEASED, nullptr);
        lv_obj_add_event_cb(btn, pressInvalidateCb, LV_EVENT_PRESS_LOST, nullptr);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_ADV_HITTEST);
        lv_obj_add_event_cb(btn, cruiseLHitTestCb, LV_EVENT_HIT_TEST, cornerData);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        constexpr int16_t LABEL_OFF_X = CRUISE_NOTCH_W / 2;
        constexpr int16_t LABEL_OFF_Y = CRUISE_NOTCH_H / 2;
        const int16_t shiftX = (col == 0) ? -LABEL_OFF_X : LABEL_OFF_X;
        const int16_t shiftY = (row == 0) ? -LABEL_OFF_Y : LABEL_OFF_Y;

        if (lv_obj_get_child_cnt(btn) > 0) {
            lv_obj_t *innerLabel = lv_obj_get_child(btn, 0);
            if (innerLabel) {
                lv_obj_add_flag(innerLabel, LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_t *label = lv_label_create(screen);
        lv_label_set_text(label, CRUISE_BUTTONS[i].label);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFFu), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        const bool isSymbol = (i == static_cast<uint8_t>(CruiseCorner::kTL)) ||
                              (i == static_cast<uint8_t>(CruiseCorner::kTR));
        lv_obj_set_style_text_font(
            label, isSymbol ? FontManager::primary(32) : FontManager::secondary(24), 0);
        const int16_t cx = x + (CRUISE_BUTTON_W / 2) + shiftX;
        const int16_t cy = y + (CRUISE_BUTTON_H / 2) + shiftY;
        lv_obj_set_pos(label, cx - (isSymbol ? 12 : 24), cy - (isSymbol ? 16 : 12));

        if (i == static_cast<uint8_t>(CruiseCorner::kBR)) {
            s_cruiseToggleBtn = btn;
            s_cruiseToggleLabel = label;
            lv_obj_add_event_cb(btn, cruiseToggleClickCb, LV_EVENT_CLICKED, nullptr);
        }
        ++created;
    }

    const int16_t centerX = (LV_HOR_RES - CRUISE_CENTER_W) / 2;
    const int16_t centerY = contentY + (contentH - CRUISE_CENTER_H) / 2;

    lv_obj_t *center = lv_obj_create(screen);
    lv_obj_set_pos(center, centerX, centerY);
    lv_obj_set_size(center, CRUISE_CENTER_W, CRUISE_CENTER_H);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_pad_all(center, 0, 0);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(center, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *setHeader = lv_label_create(center);
    lv_label_set_text(setHeader, "SET");
    lv_obj_set_style_text_color(setHeader, lv_color_hex(CRUISE_CENTER_DIM_RGB), 0);
    lv_obj_set_style_text_font(setHeader, FontManager::label(12), 0);
    lv_obj_set_width(setHeader, CRUISE_CENTER_W);
    lv_obj_set_style_text_align(setHeader, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(setHeader, 0, 4);

    lv_obj_t *setValue = lv_label_create(center);

    lv_label_set_text(setValue, "0");
    lv_obj_set_style_text_color(setValue, lv_color_hex(CRUISE_CENTER_VALUE_RGB), 0);
    lv_obj_set_style_text_font(setValue, FontManager::primary(32), 0);
    lv_obj_set_width(setValue, CRUISE_CENTER_W);
    lv_obj_set_style_text_align(setValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(setValue, 0, (CRUISE_CENTER_H - 32) / 2);

    lv_obj_t *setUnit = lv_label_create(center);
    lv_label_set_text(setUnit, "km/h");
    lv_obj_set_style_text_color(setUnit, lv_color_hex(CRUISE_CENTER_DIM_RGB), 0);
    lv_obj_set_style_text_font(setUnit, FontManager::label(12), 0);
    lv_obj_set_width(setUnit, CRUISE_CENTER_W);
    lv_obj_set_style_text_align(setUnit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(setUnit, 0, CRUISE_CENTER_H - 16);

    LOG_INFO("UI", "Built cruise_control template on page '%s' (%u/4 buttons + SET display)",
             cfg.id, created);
}

void applyPageBackground(lv_obj_t *screen, const CfgPage &cfg, const CfgColor &effectiveBg) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(effectiveBg.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    if (strlen(cfg.bgImagePath) > 0) {

        static char lvglPath[CFG_MAX_PATH_LEN + 4];
        snprintf(lvglPath, sizeof(lvglPath), "S:%s", cfg.bgImagePath);

        lv_obj_t *img = lv_img_create(screen);
        lv_img_set_src(img, lvglPath);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_opa(img, LV_OPA_COVER, LV_PART_MAIN);
        LOG_DEBUG("UI", "Background image: %s", lvglPath);
    }
}

} // namespace

void buildPage(uint8_t idx, const CfgPage &cfg) {
    LOG_INFO("UI", "buildPage(%s) entry: heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    Page &p = s_pages[idx];
    strlcpy(p.id, cfg.id, CFG_MAX_ID_LEN);

    p.screen = lv_obj_create(nullptr);
    lv_obj_set_size(p.screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(p.screen, LV_OBJ_FLAG_SCROLLABLE);

    applyPageBackground(p.screen, cfg, ThemeManager::getEffectiveBgColor(cfg.bgColor));

    int16_t contentY = cfg.showTopBar ? TopBar::getHeight() : 0;

    if (cfg.templateKind == CfgPageTemplate::CRUISE_CONTROL) {
        buildCruiseControlTemplate(p.screen, cfg, contentY);
        p.built = true;
        if (cfg.widgetCount > 0) {
            LOG_INFO("UI", "Page '%s': cruise_control template — ignoring %u user widget(s)",
                     cfg.id, static_cast<unsigned>(cfg.widgetCount));
        }
        LOG_INFO("UI", "buildPage(%s) exit:  heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(ESP.getFreeHeap()),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        return;
    }

    uint8_t created = 0;
    for (uint8_t w = 0; w < cfg.widgetCount; ++w) {
        const CfgWidget &wCfg = cfg.widgets[w];
        if (WidgetFactory::create(p.screen, wCfg, contentY) != nullptr)
            ++created;
    }

    p.built = true;
    if (created < cfg.widgetCount) {
        LOG_WARN("UI", "Page '%s': only %u/%u widgets built — see prior WF errors", cfg.id,
                 static_cast<unsigned>(created), static_cast<unsigned>(cfg.widgetCount));
    } else {
        LOG_INFO("UI", "Built page '%s' with %u widgets", cfg.id,
                 static_cast<unsigned>(cfg.widgetCount));
    }

    LOG_INFO("UI", "buildPage(%s) exit:  heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void reapplyThemeAllPages() {
    s_rebuildRequested = false;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || s_pageCount == 0)
        return;

    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (!s_pages[i].screen)
            continue;
        const CfgPage &cfg = dash.pages[s_pages[i].cfgIdx];
        const CfgColor effectiveBg = ThemeManager::getEffectiveBgColor(cfg.bgColor);
        lv_obj_set_style_bg_color(s_pages[i].screen, lv_color_hex(effectiveBg.rgb), LV_PART_MAIN);
        WidgetFactory::reapplyTheme(s_pages[i].screen);
    }

    TopBar::reapplyTheme();
}

void rebuildAllPages() {
    s_rebuildRequested = false;
    s_pendingFreeIdx = 0xFF;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || s_pageCount == 0)
        return;

    IconAssets::preloadDashboardAssets();

    uint8_t savedIdx = s_currentIdx;

    lv_obj_t *dummy = lv_obj_create(nullptr);
    lv_scr_load(dummy);

    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (s_pages[i].screen) {
            WidgetFactory::clearAll(s_pages[i].screen);
            lv_obj_del(s_pages[i].screen);
            s_pages[i].screen = nullptr;
            s_pages[i].built = false;
        }
    }

    if (savedIdx < s_pageCount) {
        buildPage(savedIdx, dash.pages[s_pages[savedIdx].cfgIdx]);
    }

    uint8_t loadedIdx = 0xFF;
    if (savedIdx < s_pageCount && s_pages[savedIdx].screen) {
        loadedIdx = savedIdx;
    } else {
        for (uint8_t i = 0; i < s_pageCount; ++i) {
            if (i == savedIdx)
                continue;
            buildPage(i, dash.pages[s_pages[i].cfgIdx]);
            if (s_pages[i].screen) {
                loadedIdx = i;
                break;
            }
        }
    }

    if (loadedIdx != 0xFF) {
        lv_scr_load(s_pages[loadedIdx].screen);
        s_currentIdx = loadedIdx;
        lv_obj_del(dummy);
    } else {

        LOG_ERROR("UI", "rebuildAllPages: no page could be rebuilt — staging "
                        "screen kept active to avoid LVGL UAF");
    }

    IconAssets::preloadDashboardAssets();

    TopBar::reapplyTheme();

    LOG_INFO("UI", "Pages rebuilt for theme toggle");
}

} // namespace PageManagerInternal
