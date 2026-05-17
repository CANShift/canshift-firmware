// button_widget.cpp — Tap-action button (page nav, ECU map switch, raw CAN, …)

#include "button_widget.h"
#include "app_config.h"
#include "config/config_loader.h"
#include "runtime/action_dispatcher.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "diag/logger.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

// Maximum LVGL FS path length including the "S:" SPIFFS drive prefix.
constexpr size_t LVGL_PATH_LEN = 2 + CFG_MAX_PATH_LEN;

const lv_font_t *selectButtonFont(int16_t h) {
    if (h >= 56)
        return FontManager::secondary(24);
    if (h >= 40)
        return FontManager::secondary(20);
    if (h >= 28)
        return FontManager::label(16);
    if (h >= 20)
        return FontManager::label(14);
    return FontManager::label(12);
}

static constexpr int16_t MAP_BADGE_DIAMETER = 7;
static constexpr uint32_t MAP_BADGE_COLOR = 0x33CC44; // green

// Per-button runtime state — owns the latched toggle flag and a pointer back
// to the const config (kept alive by the dashboard singleton).
struct ButtonTag {
    const CfgWidget *cfg; // For style.primaryColor when computing derived toggle visuals (#838)
    const CfgButtonParams *params;
    lv_obj_t *iconImg;                 // nullptr when no asset is rendered
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
constexpr uint32_t TOGGLE_DERIVED_BORDER = 0xFFFFFFu;

uint32_t lightenRgb(uint32_t rgb, uint32_t delta) {
    const uint32_t r = (rgb >> 16) & 0xFF;
    const uint32_t g = (rgb >> 8) & 0xFF;
    const uint32_t b = rgb & 0xFF;
    const uint32_t rL = r + delta > 0xFF ? 0xFF : r + delta;
    const uint32_t gL = g + delta > 0xFF ? 0xFF : g + delta;
    const uint32_t bL = b + delta > 0xFF ? 0xFF : b + delta;
    return (rL << 16) | (gL << 8) | bL;
}

// Resting / active colours for a toggle button. When the dashboard provides
// an explicit `colors` block we honour it verbatim. Otherwise we derive an
// active tint from the widget's primary colour AND surface a 1 px border to
// give the latched state a second visual cue (issue #838). Both branches
// share the same surface so the caller doesn't have to special-case.
struct ToggleVisual {
    uint32_t bg;
    bool showBorder;
};

ToggleVisual computeToggleVisual(const CfgWidget &cfg, const CfgButtonParams &p, bool active) {
    if (p.hasColors) {
        return {active ? p.colorActive.rgb : p.colorNormal.rgb, false};
    }
    if (active) {
        return {lightenRgb(cfg.style.primaryColor.rgb, TOGGLE_DERIVED_ACTIVE_DELTA), true};
    }
    return {cfg.style.primaryColor.rgb, false};
}

void applyToggleVisualState(lv_obj_t *btn, const ButtonTag &tag) {
    if (!tag.params || !tag.params->isToggle || !tag.cfg)
        return;
    const ToggleVisual v = computeToggleVisual(*tag.cfg, *tag.params, tag.toggleActive);
    lv_obj_set_style_bg_color(btn, lv_color_hex(v.bg), LV_PART_MAIN);
    if (v.showBorder) {
        lv_obj_set_style_border_color(btn, lv_color_hex(TOGGLE_DERIVED_BORDER), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
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
        tag->signalSyncIgnoreUntilMs = millis() + BUTTON_SIGNAL_SYNC_GRACE_MS;
    }

    for (uint8_t i = 0; i < tag->params->actionsCount; ++i) {
        ActionDispatcher::dispatchAction(tag->params->actions[i], tag->toggleActive);
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
    LOG_DEBUG("BTN", "create %s heap.largest=%u", cfg.id,
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    tag->cfg = &cfg;
    tag->params = &p;
    tag->iconImg = nullptr;
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

    const lv_font_t *btnFont = selectButtonFont(cfg.layout.h);

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
            lv_obj_set_style_img_recolor(tag->iconImg, lv_color_hex(cfg.style.textColor.rgb), 0);
            lv_obj_set_style_img_recolor_opa(tag->iconImg, LV_OPA_COVER, 0);
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
        lv_obj_set_style_text_color(label, lv_color_hex(cfg.style.textColor.rgb), 0);
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
    lv_obj_add_event_cb(btn, btnDeleteHandler, LV_EVENT_DELETE, tag);

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
