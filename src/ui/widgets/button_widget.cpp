// button_widget.cpp — Tap-action button (page nav, ECU map switch, raw CAN, …)

#include "button_widget.h"
#include "config/config_loader.h"
#include "runtime/action_dispatcher.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/screen_profile.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "diag/logger.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// Maximum LVGL FS path length including the "S:" SPIFFS drive prefix.
constexpr size_t LVGL_PATH_LEN = 2 + CFG_MAX_PATH_LEN;

// Studio reference (widget-previews/Button.tsx::computeButtonPreviewMetrics):
// column layout, icon on top sized to fill ~0.75 of the cell, label below at
// Medium 500 weight, font driven by width budget (not height).
// TODO(#18): thresholds + font snap target are hard-coded against the v1
// 320×240 canvas. When a second screen profile lands, scale with
// `ScreenProfile::scaleYVal` and grow the Orbitron ladder in FontManager.

constexpr int16_t BUTTON_PAD_X = 6;
constexpr int16_t BUTTON_PAD_Y = 4;
constexpr int16_t BUTTON_ROW_GAP = 2;
constexpr int16_t ICON_MIN_PX = 18;
constexpr int16_t ICON_MAX_PX = 56;
constexpr int16_t ICON_H_BUDGET_DROP = 14; // matches Studio's `h - 14` term
constexpr float ICON_H_RATIO = 0.75f;
constexpr float ICON_W_RATIO = 0.70f;
constexpr float LABEL_NO_ICON_VBUDGET_RATIO = 0.48f;
constexpr float LABEL_WITH_ICON_VBUDGET_RATIO = 0.20f;
constexpr float LABEL_ICON_FRACTION = 0.40f;
constexpr float LABEL_W_RATIO = 0.22f;
constexpr int16_t LABEL_FONT_MIN = 8;
constexpr int16_t LABEL_BUDGET_PAD = 12; // matches Studio's `w - 12`

int16_t computeIconSize(int16_t w, int16_t h) {
    int16_t budget = static_cast<int16_t>(h * ICON_H_RATIO);
    if (h - ICON_H_BUDGET_DROP < budget)
        budget = static_cast<int16_t>(h - ICON_H_BUDGET_DROP);
    const int16_t widthCap = static_cast<int16_t>(w * ICON_W_RATIO);
    if (widthCap < budget)
        budget = widthCap;
    if (budget > ICON_MAX_PX)
        budget = ICON_MAX_PX;
    if (budget < ICON_MIN_PX)
        budget = ICON_MIN_PX;
    return budget;
}

int16_t computeLabelFontSize(int16_t w, int16_t h, bool showIcon, int16_t iconSize) {
    const float verticalBudget =
        showIcon ? fminf(h * LABEL_WITH_ICON_VBUDGET_RATIO, iconSize * LABEL_ICON_FRACTION)
                 : h * LABEL_NO_ICON_VBUDGET_RATIO;
    const float labelBudget = static_cast<float>(w - LABEL_BUDGET_PAD);
    float fontSize = fminf(verticalBudget, labelBudget * LABEL_W_RATIO);
    if (fontSize < LABEL_FONT_MIN)
        fontSize = LABEL_FONT_MIN;
    return static_cast<int16_t>(fontSize);
}

// Pick the nearest baked Medium tier {12, 14, 16} for the target px size.
// FontManager::label() snaps DOWN, so we round to nearest explicitly: any
// target ≥ 15 picks 16, ≥ 13 picks 14, otherwise 12. Studio always uses
// fontWeight: 500 (Medium) so we stay in the label tier regardless of size.
const lv_font_t *selectButtonFontFromTarget(int16_t targetPx) {
    if (targetPx >= 15)
        return FontManager::label(16);
    if (targetPx >= 13)
        return FontManager::label(14);
    return FontManager::label(12);
}

static constexpr int16_t MAP_BADGE_DIAMETER = 7;
static constexpr uint32_t MAP_BADGE_COLOR = 0x33CC44; // green

// Translucency to mirror Studio's idle/active alpha tints
// (normalColor + '18' ≈ 9 %, activeColor + '55' ≈ 33 %).
// LV_OPA_10 (= 25 ≈ 0x19) is the closest baked tier to 0x18; the active
// tint uses a raw 0x55 because LV_OPA_30 (= 76) snaps below the Studio value.
constexpr lv_opa_t BUTTON_BG_OPA_IDLE = LV_OPA_10;
constexpr lv_opa_t BUTTON_BG_OPA_ACTIVE = 0x55;
constexpr lv_opa_t BUTTON_ICON_OPA = 0xCC; // matches Studio textColor + 'CC'
constexpr int16_t BUTTON_BORDER_WIDTH = 1;

// Per-button runtime state — owns the latched toggle flag and a pointer back
// to the const config (kept alive by the dashboard singleton).
struct ButtonTag {
    const CfgWidget *cfg; // For style.primaryColor when computing derived toggle visuals (#838)
    const CfgButtonParams *params;
    lv_obj_t *iconImg;                 // nullptr when no asset is rendered
    lv_obj_t *labelObj;                // nullptr when label is hidden
    bool toggleActive;                 // only meaningful when params->isToggle == true
    char lvglPath[LVGL_PATH_LEN];      // resolved LVGL FS path for the icon, "" if none
    lv_obj_t *activeBadge;             // green dot overlay, nullptr if not a map_switch button
    uint8_t mapSwitchIndex;            // mapIndex of the MAP_SWITCH action, 0 = none
    char signalId[CFG_MAX_SIGNAL_LEN]; // signal driving toggle state; "" = use local latch
    uint32_t signalSyncIgnoreUntilMs;  // ms tick before which update() must skip signal-driven sync
};

// Resolve the icon source. Returns a non-empty C-string LVGL path when an
// asset exists for the widget, or "" when no icon will be drawn (see #681 —
// the LVGL symbol fallback was removed). Prefers user-provided iconPath over
// the built-in iconName lookup.
//
// Both branches probe the underlying file before returning: LVGL silently
// no-ops on lv_img_set_src for a missing file, so without the probe the
// widget would render an empty box.
const char *resolveIconAsset(const CfgButtonParams &p, char *out, size_t outLen) {
    out[0] = '\0';
    if (p.iconPath[0] != '\0') {
        // Studio supplies SPIFFS paths with a leading slash already.
        const char *prefix = (p.iconPath[0] == '/') ? "" : "/";
        snprintf(out, outLen, "S:%s%s", prefix, p.iconPath);
        if (IconAssets::exists(out))
            return out;
        out[0] = '\0';
        return out;
    }
    const char *assetPath = IconAssets::path(p.iconName);
    if (assetPath[0] != '\0') {
        strlcpy(out, assetPath, outLen);
        return out;
    }
    return out; // empty
}

// Brighten an RGB colour by a fixed per-channel delta (saturating). Used to
// derive an "active" tint from a toggle button's resting colour when the
// dashboard config did not provide an explicit `colors{normal,active}` block
// (issue #838). Keeps the resting appearance untouched and only shifts the
// pressed state, so existing dashboards don't suddenly look different.
constexpr uint32_t TOGGLE_DERIVED_ACTIVE_DELTA = 0x40; // ~25 % brighter

uint32_t lightenRgb(uint32_t rgb, uint32_t delta) {
    const uint32_t r = (rgb >> 16) & 0xFF;
    const uint32_t g = (rgb >> 8) & 0xFF;
    const uint32_t b = rgb & 0xFF;
    const uint32_t rL = r + delta > 0xFF ? 0xFF : r + delta;
    const uint32_t gL = g + delta > 0xFF ? 0xFF : g + delta;
    const uint32_t bL = b + delta > 0xFF ? 0xFF : b + delta;
    return (rL << 16) | (gL << 8) | bL;
}

// Resting / active visual for a button. Mirrors Studio's per-state palette
// (widget-previews/Button.tsx): translucent bg, 1-px border in stateColor /
// secondaryColor, icon + label tinted with the matching stateColor. The
// derived-active path (no explicit `colors` block) still lightens primary
// for legacy two-state behaviour from #838.
struct ButtonVisual {
    uint32_t bgColor;
    lv_opa_t bgOpa;
    uint32_t borderColor;
    uint32_t textColor;
};

ButtonVisual computeButtonVisual(const CfgWidget &cfg, const CfgButtonParams &p, bool active) {
    const uint32_t normalColor = p.hasColors ? p.colorNormal.rgb : cfg.style.primaryColor.rgb;
    const uint32_t activeColor =
        p.hasColors ? p.colorActive.rgb
                    : lightenRgb(cfg.style.primaryColor.rgb, TOGGLE_DERIVED_ACTIVE_DELTA);
    ButtonVisual v;
    if (active) {
        v.bgColor = activeColor;
        v.bgOpa = BUTTON_BG_OPA_ACTIVE;
        v.borderColor = activeColor;
        v.textColor = activeColor;
    } else {
        v.bgColor = normalColor;
        v.bgOpa = BUTTON_BG_OPA_IDLE;
        v.borderColor = cfg.style.secondaryColor.rgb;
        v.textColor = cfg.style.textColor.rgb;
    }
    return v;
}

void applyButtonVisual(lv_obj_t *btn, const ButtonTag &tag, const ButtonVisual &v) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(v.bgColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, v.bgOpa, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(v.borderColor), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, BUTTON_BORDER_WIDTH, LV_PART_MAIN);
    if (tag.iconImg) {
        lv_obj_set_style_img_recolor(tag.iconImg, lv_color_hex(v.textColor), 0);
        lv_obj_set_style_img_recolor_opa(tag.iconImg, BUTTON_ICON_OPA, 0);
    }
    if (tag.labelObj) {
        lv_obj_set_style_text_color(tag.labelObj, lv_color_hex(v.textColor), 0);
    }
}

void applyToggleVisualState(lv_obj_t *btn, const ButtonTag &tag) {
    if (!tag.cfg || !tag.params)
        return;
    const bool active = tag.params->isToggle && tag.toggleActive;
    const ButtonVisual v = computeButtonVisual(*tag.cfg, *tag.params, active);
    applyButtonVisual(btn, tag, v);
}

void btnClickHandler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    auto *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    auto *tag = static_cast<ButtonTag *>(lv_obj_get_user_data(btn));
    if (!tag || !tag->params)
        return;

    if (tag->params->isToggle) {
        tag->toggleActive = !tag->toggleActive;
        applyToggleVisualState(btn, *tag);
        tag->signalSyncIgnoreUntilMs = millis() + BUTTON_SIGNAL_SYNC_GRACE_MS;

        // Optimistic SignalStore write — mirror the new local latch into the
        // signal the button is bound to so any other widget reading that
        // signal (DiagDrawer ECU FLAGS row, status indicators, etc.) reflects
        // the press immediately instead of waiting for the ECU to echo back
        // through CAN. When CAN feedback arrives, `SignalStore::update`
        // overrides this value with the ground truth — no flicker because
        // the values match. With CAN unavailable (bench testing, bus off)
        // the local value is the only feedback the user gets.
        if (tag->signalId[0] != '\0') {
            const SignalId sid = signalIdFromName(tag->signalId);
            if (sid < SignalIds::SIGNAL_COUNT) {
                SignalStore::update(sid, tag->toggleActive ? 1.0f : 0.0f);
            }
        }
    }

    for (uint8_t i = 0; i < tag->params->actionsCount; ++i) {
        ActionDispatcher::dispatchAction(tag->params->actions[i], tag->toggleActive);
    }
}

// ButtonTag storage comes from the shared WidgetTagPool slab (#1031
// F-HI-2 follow-up). See ui/widgets/widget_tag_pool.h.

} // namespace

lv_obj_t *ButtonWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *btn = lv_btn_create(parent);
    // Design-space → physical coordinate scaling (issues #17, #18). Identity
    // on the only v1 profile (`crowpanel-28`), so output is byte-identical.
    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(btn, px, py);
    lv_obj_set_size(btn, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));

    const CfgButtonParams &p = cfg.button;

    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, BUTTON_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, BUTTON_PAD_Y, LV_PART_MAIN);

    // Column layout (icon on top, label below) mirroring Studio's
    // widget-previews/Button.tsx column flex.
    const bool hasIcon = p.showIcon && (p.iconPath[0] != '\0' || p.iconName[0] != '\0');
    const bool hasLabel = p.showLabel && p.label[0] != '\0';

    if (hasIcon || hasLabel) {
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(btn, BUTTON_ROW_GAP, LV_PART_MAIN);
    }

    ButtonTag *tag = WidgetTagPool::alloc<ButtonTag>();
    if (!tag) {
        LOG_WARN("BTN", "Tag pool exhausted for '%s' (all %u slots busy)", cfg.id,
                 static_cast<unsigned>(WidgetTagPool::kPoolSlots));
        lv_obj_del(btn);
        return nullptr;
    }
    LOG_DEBUG("BTN", "create %s heap.largest=%u", cfg.id,
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    tag->cfg = &cfg;
    tag->params = &p;
    tag->iconImg = nullptr;
    tag->labelObj = nullptr;
    tag->toggleActive = false;
    tag->lvglPath[0] = '\0';
    tag->activeBadge = nullptr;
    tag->mapSwitchIndex = 0;
    strlcpy(tag->signalId, cfg.signalId, sizeof(tag->signalId));
    tag->signalSyncIgnoreUntilMs = 0;

    // Resolve map_switch index before layout so badge creation can reference it
    for (uint8_t i = 0; i < p.actionsCount; ++i) {
        if (p.actions[i].type == CfgButtonActionType::MAP_SWITCH) {
            tag->mapSwitchIndex = p.actions[i].mapIndex;
            break;
        }
    }

    const int16_t targetIconSize = hasIcon ? computeIconSize(cfg.layout.w, cfg.layout.h) : 0;
    const int16_t targetFontSize =
        computeLabelFontSize(cfg.layout.w, cfg.layout.h, hasIcon, targetIconSize);
    const lv_font_t *btnFont = selectButtonFontFromTarget(targetFontSize);

    if (hasIcon) {
        const char *path = resolveIconAsset(p, tag->lvglPath, sizeof(tag->lvglPath));
        // Heap guard: under fragmentation the SPIFFS icon load via
        // lv_img_set_src → lv_fs_open → newlib fopen can abort(). Mirrors the
        // gate in lvgl_fs_driver.cpp::fs_open so the icon path bails out at
        // the widget layer too. Closes the OOM suspect in #717 / #651 / #660.
        const size_t poolLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        const bool heapOk = (poolLargest >= LVGL_FS_MIN_HEAP_BYTES);
        if (path[0] != '\0' && heapOk) {
            tag->iconImg = lv_img_create(btn);
            lv_img_set_src(tag->iconImg, tag->lvglPath);
            // Pin the layout slot to the Studio-computed target size. lv_img
            // defaults to `LV_SIZE_CONTENT`, reporting its self-size as the
            // *unzoomed* native dims under `LV_IMG_SIZE_MODE_VIRTUAL`. Inside
            // a column flex parent that left the layout slot decoupled from
            // the zoomed rendered size, so when the SPIFFS decode in
            // `lv_img_set_src` failed silently (heap-low FS gate, missing
            // header) `img->w` stayed at 0 and the slot collapsed — icon
            // never appeared (#1242). Forcing the size also guarantees the
            // flex parent reserves exactly the painted region regardless of
            // when the .bin header is decoded.
            lv_obj_set_size(tag->iconImg, targetIconSize, targetIconSize);
            // Derive zoom from native dims so the source paints to the
            // explicit slot size. LVGL populates source dims synchronously
            // inside `lv_img_set_src` via `lv_img_decoder_get_info`, so the
            // read below is safe at create time for any decoded asset.
            // zoom = (target / native) * 256, with 256 = 1:1.
            const lv_coord_t nativeW = lv_obj_get_self_width(tag->iconImg);
            const lv_coord_t nativeH = lv_obj_get_self_height(tag->iconImg);
            const lv_coord_t nativeMax = nativeW > nativeH ? nativeW : nativeH;
            if (nativeMax > 0) {
                uint32_t zoom = (static_cast<uint32_t>(targetIconSize) * 256u) /
                                static_cast<uint32_t>(nativeMax);
                if (zoom < 1)
                    zoom = 1;
                if (zoom > 0xFFFFu)
                    zoom = 0xFFFFu;
                lv_img_set_zoom(tag->iconImg, static_cast<uint16_t>(zoom));
            }
        } else if (path[0] != '\0' && !heapOk) {
            // Asset is on disk but heap is too fragmented to load it — skip
            // and log once. The button still renders its label/colour cues.
            LOG_WARN("BTN", "skipping icon %s — largest=%u below threshold", tag->lvglPath,
                     static_cast<unsigned>(poolLargest));
            tag->lvglPath[0] = '\0';
        }
        // No glyph fallback (#681): Orbitron does not cover the LVGL symbol
        // range, so an "empty" label just consumed layout slots for nothing.
    }

    if (hasLabel) {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, p.label);
        lv_obj_set_style_text_font(label, btnFont, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        tag->labelObj = label;
    }

    // Apply idle visual (translucent bg + 1-px border + recoloured icon/label).
    // computeButtonVisual mirrors Studio's idle/active per-state palette so
    // toggle clicks (applyToggleVisualState) and PRESSED feedback below stay
    // consistent.
    {
        const ButtonVisual idle = computeButtonVisual(cfg, p, false);
        applyButtonVisual(btn, *tag, idle);
        if (!p.isToggle) {
            // Momentary buttons still flash via LVGL's PRESSED state so the
            // user gets immediate feedback on tap. Toggle buttons drive the
            // active visual themselves from the latched flag (see
            // applyToggleVisualState) and ignore PRESSED.
            const ButtonVisual pressed = computeButtonVisual(cfg, p, true);
            lv_obj_set_style_bg_color(btn, lv_color_hex(pressed.bgColor),
                                      LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(btn, pressed.bgOpa, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_border_color(btn, lv_color_hex(pressed.borderColor),
                                          LV_PART_MAIN | LV_STATE_PRESSED);
        }
    }

    // Active-map badge — green dot in top-right corner, shown when MAP_NUMBER
    // matches this button's mapIndex. Only created for map_switch buttons.
    if (tag->mapSwitchIndex != 0) {
        lv_obj_t *badge = lv_obj_create(btn);
        lv_obj_set_size(badge, MAP_BADGE_DIAMETER, MAP_BADGE_DIAMETER);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(badge, lv_color_hex(MAP_BADGE_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(badge, 0, LV_PART_MAIN);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -2, 2);
        lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
        tag->activeBadge = badge;
    }

    lv_obj_set_user_data(btn, tag);
    lv_obj_add_event_cb(btn, btnClickHandler, LV_EVENT_CLICKED, nullptr);
    WidgetHelpers::attachTagDeleter(btn, tag);

    return btn;
}

void ButtonWidget::update(lv_obj_t *btn) {
    auto *tag = static_cast<ButtonTag *>(lv_obj_get_user_data(btn));
    if (!tag)
        return;

    // Signal-driven toggle: sync visual state from ECU signal so it survives
    // page changes (the local latch resets on widget destruction). The grace
    // window after a click keeps the local latch authoritative until the ECU
    // echo can catch up — prevents flicker and re-arm on re-tap (issue #658).
    if (tag->params && tag->params->isToggle && tag->signalId[0] != '\0' &&
        millis() >= tag->signalSyncIgnoreUntilMs) {
        const SignalId sid = signalIdFromName(tag->signalId);
        const bool active = sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid) &&
                            SignalStore::read(sid, 0.0f) != 0.0f;
        if (active != tag->toggleActive) {
            tag->toggleActive = active;
            applyToggleVisualState(btn, *tag);
        }
    }

    // Active-map badge for map_switch buttons.
    if (!tag->activeBadge || tag->mapSwitchIndex == 0)
        return;

    const bool active =
        SignalStore::isValid(SignalIds::MAP_NUMBER) &&
        static_cast<uint8_t>(SignalStore::read(SignalIds::MAP_NUMBER)) == tag->mapSwitchIndex;

    if (active) {
        lv_obj_clear_flag(tag->activeBadge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(tag->activeBadge, LV_OBJ_FLAG_HIDDEN);
    }
}
