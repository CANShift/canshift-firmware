// button_widget.cpp — Tap-action button (page nav, ECU map switch, raw CAN, …)

#include "button_widget.h"
#include "app_config.h"
#include "can/can_manager.h"
#include "can_signals_out.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/page_manager.h"
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

void applyToggleVisualState(lv_obj_t *btn, const ButtonTag &tag) {
    if (!tag.params || !tag.params->isToggle || !tag.params->hasColors)
        return;
    const uint32_t bg =
        tag.toggleActive ? tag.params->colorActive.rgb : tag.params->colorNormal.rgb;
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), LV_PART_MAIN);
}

void dispatchAction(const CfgButtonAction &a, bool isActive) {
    switch (a.type) {
        case CfgButtonActionType::NAV_PAGE:
            if (a.pageId[0] != '\0') {
                // Defer navigateTo to the next LVGL tick: it can synchronously
                // free the screen this button lives on (lazy-build path releases
                // the departing page), and the event loop would otherwise return
                // into freed memory. lv_async_call drains at the top of the next
                // lv_timer_handler() iteration, after this click handler unwinds.
                // Closes the re-entrant navigation path in #717.
                static char s_pendingNavId[CFG_MAX_ID_LEN];
                strlcpy(s_pendingNavId, a.pageId, sizeof(s_pendingNavId));
                lv_async_call(
                    [](void *p) {
                        const char *id = static_cast<const char *>(p);
                        PageManager::navigateTo(id);
                    },
                    s_pendingNavId);
            }
            break;
        case CfgButtonActionType::MAP_SWITCH: {
            // Frame ID is sourced from signals.json `out.map_switch.id` (issue
            // #317) and falls back to the baked default in can_signals_out.h
            // when the user hasn't provided an override. Either way it remains
            // ECU-specific and unverified — warn once so field testers see it.
            const CfgSignalsOut &outCfg = ConfigLoader::getSignalConfig().out;
            const uint32_t frameId =
                outCfg.mapSwitchFrameId != 0 ? outCfg.mapSwitchFrameId : CAN_OUT_MAP_SWITCH_ID;
            const bool extended = outCfg.mapSwitchExtended;
            static bool s_warnedMapSwitchUnverified = false;
            if (!s_warnedMapSwitchUnverified) {
                LOG_WARN("BTN",
                         "map_switch frame id=0x%lX (ext=%d) is UNVERIFIED — confirm against ECU",
                         static_cast<unsigned long>(frameId), extended ? 1 : 0);
                s_warnedMapSwitchUnverified = true;
            }
            if (a.mapIndex < MAP_SWITCH_MIN_INDEX || a.mapIndex > MAP_SWITCH_MAX_INDEX) {
                LOG_WARN("BTN", "map_switch: mapIndex=%u out of range [%u,%u] — dropped",
                         static_cast<unsigned>(a.mapIndex),
                         static_cast<unsigned>(MAP_SWITCH_MIN_INDEX),
                         static_cast<unsigned>(MAP_SWITCH_MAX_INDEX));
                break;
            }
            const uint8_t payload[CAN_OUT_MAP_SWITCH_DLC] = {a.mapIndex};
            (void)CanManager::sendFrame(frameId, payload, CAN_OUT_MAP_SWITCH_DLC, extended);
            break;
        }
        case CfgButtonActionType::CAN_RAW:
            // Pass through the parsed extended flag (issue #319). Parser
            // auto-promotes any ID >0x7FF to extended so legacy configs that
            // omit the flag still transmit a valid frame.
            // When disarming a toggle and a dataOff payload was configured, send
            // the off-frame instead of the arm-frame.
            if (!isActive && a.canDataOffLen > 0) {
                (void)CanManager::sendFrame(a.canFrameId, a.canDataOff, a.canDataOffLen,
                                            a.canExtended);
            } else {
                (void)CanManager::sendFrame(a.canFrameId, a.canData, a.canDataLen, a.canExtended);
            }
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
        tag->signalSyncIgnoreUntilMs = millis() + BUTTON_SIGNAL_SYNC_GRACE_MS;
    }

    for (uint8_t i = 0; i < tag->params->actionsCount; ++i) {
        dispatchAction(tag->params->actions[i], tag->toggleActive);
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
