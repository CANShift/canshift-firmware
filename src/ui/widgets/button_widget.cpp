// button_widget.cpp — Tap-action button (page nav, ECU map switch, raw CAN, …)

#include "button_widget.h"
#include "ui/icon_assets.h"
#include "ui/page_manager.h"
#include "diag/logger.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

// Maximum LVGL FS path length including the "S:" prefix used for SD-card assets.
constexpr size_t LVGL_PATH_LEN = 2 + CFG_MAX_PATH_LEN;

// Per-button runtime state — owns the latched toggle flag and a pointer back
// to the const config (kept alive by the dashboard singleton).
struct ButtonTag {
    const CfgButtonParams *params;
    lv_obj_t *iconImg;   // nullptr if no asset rendered
    lv_obj_t *iconLabel; // glyph fallback (nullptr if asset rendered or showIcon=false)
    bool toggleActive;   // only meaningful when params->isToggle == true
    char lvglPath[LVGL_PATH_LEN]; // resolved LVGL FS path for the icon, "" if none
};

// Resolve the icon source. Returns a non-empty C-string LVGL path when an
// asset exists for the widget, or "" when only a glyph fallback is available.
// Prefers user-provided iconPath over the built-in iconName lookup.
//
// Both branches probe the underlying file before returning: LVGL silently
// no-ops on lv_img_set_src for a missing file, so without the probe the
// widget would render an empty box instead of the LV_SYMBOL_* fallback.
const char *resolveIconAsset(const CfgButtonParams &p, char *out, size_t outLen) {
    out[0] = '\0';
    if (p.iconPath[0] != '\0') {
        // Studio supplies SPIFFS / SD paths with a leading slash already.
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

void applyToggleVisualState(lv_obj_t *btn, const ButtonTag &tag) {
    if (!tag.params || !tag.params->isToggle || !tag.params->hasColors)
        return;
    const uint32_t bg =
        tag.toggleActive ? tag.params->colorActive.rgb : tag.params->colorNormal.rgb;
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), LV_PART_MAIN);
}

void dispatchAction(const CfgButtonAction &a) {
    switch (a.type) {
        case CfgButtonActionType::NAV_PAGE:
            if (a.pageId[0] != '\0')
                PageManager::navigateTo(a.pageId);
            break;
        case CfgButtonActionType::MAP_SWITCH:
            // TODO(#XXX): wire ECU map_switch dispatch — tracked as follow-up.
            LOG_INFO("BTN", "map_switch action (mapIndex=%u) not yet implemented",
                     static_cast<unsigned>(a.mapIndex));
            break;
        case CfgButtonActionType::CAN_RAW:
            // TODO(#XXX): wire raw CAN frame send — tracked as follow-up.
            LOG_INFO("BTN", "can_raw action (frameId=0x%lX) not yet implemented",
                     static_cast<unsigned long>(a.canFrameId));
            break;
        case CfgButtonActionType::UNKNOWN:
        default:
            break;
    }
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
    }

    for (uint8_t i = 0; i < tag->params->actionsCount; ++i) {
        dispatchAction(tag->params->actions[i]);
    }
}

void btnDeleteHandler(lv_event_t *e) {
    auto *tag = static_cast<ButtonTag *>(lv_event_get_user_data(e));
    delete tag;
}

} // namespace

lv_obj_t *ButtonWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(btn, cfg.layout.w, cfg.layout.h);

    const CfgButtonParams &p = cfg.button;

    // Idle / pressed background — honor `colors{normal,active}` when present,
    // otherwise fall back to the legacy `widget.style.primaryColor`. Toggle
    // buttons drive the bg color manually from the latched flag (see
    // applyToggleVisualState) and ignore LVGL's PRESSED state.
    const uint32_t bgNormal = p.hasColors ? p.colorNormal.rgb : cfg.style.primaryColor.rgb;
    const uint32_t bgActive = p.hasColors ? p.colorActive.rgb : cfg.style.primaryColor.rgb;
    lv_obj_set_style_bg_color(btn, lv_color_hex(bgNormal), LV_PART_MAIN);
    if (!p.isToggle) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(bgActive), LV_PART_MAIN | LV_STATE_PRESSED);
    }
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);

    // Lay out icon (if any) and label (if any) in a row, mirroring the
    // studio's ButtonPreview (icon left, label right, centered).
    const bool hasIcon = p.showIcon && (p.iconPath[0] != '\0' || p.iconName[0] != '\0');
    const bool hasLabel = p.showLabel && p.label[0] != '\0';

    if (hasIcon || hasLabel) {
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(btn, 4, LV_PART_MAIN);
    }

    auto *tag = new ButtonTag{};
    tag->params = &p;
    tag->iconImg = nullptr;
    tag->iconLabel = nullptr;
    tag->toggleActive = false;
    tag->lvglPath[0] = '\0';

    if (hasIcon) {
        const char *path = resolveIconAsset(p, tag->lvglPath, sizeof(tag->lvglPath));
        if (path[0] != '\0') {
            tag->iconImg = lv_img_create(btn);
            lv_img_set_src(tag->iconImg, tag->lvglPath);
            lv_obj_set_style_img_recolor(tag->iconImg, lv_color_hex(cfg.style.textColor.rgb), 0);
            lv_obj_set_style_img_recolor_opa(tag->iconImg, LV_OPA_COVER, 0);
        } else {
            // No asset on disk — render the LVGL symbol fallback so the
            // button still shows an icon glyph.
            tag->iconLabel = lv_label_create(btn);
            lv_label_set_text(tag->iconLabel, IconAssets::fallbackGlyph(p.iconName));
            lv_obj_set_style_text_color(tag->iconLabel,
                                        lv_color_hex(cfg.style.textColor.rgb), 0);
        }
    }

    if (hasLabel) {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, p.label);
        lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
    }

    lv_obj_set_user_data(btn, tag);
    lv_obj_add_event_cb(btn, btnClickHandler, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, btnDeleteHandler, LV_EVENT_DELETE, tag);

    return btn;
}
