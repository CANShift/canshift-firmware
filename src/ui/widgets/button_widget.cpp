#include "button_widget.h"
#include "config/config_loader.h"
#include "runtime/action_dispatcher.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "ui/font_manager.h"
#include "ui/icon_assets.h"
#include "ui/icon_assets_baked.h"
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

constexpr size_t LVGL_PATH_LEN = 2 + CFG_MAX_PATH_LEN;

constexpr int16_t BUTTON_PAD_X = 6;
constexpr int16_t BUTTON_PAD_Y = 4;
constexpr int16_t BUTTON_ROW_GAP = 2;
constexpr int16_t ICON_MIN_PX = 18;
constexpr int16_t ICON_MAX_PX = 56;
constexpr int16_t ICON_H_BUDGET_DROP = 14;
constexpr float ICON_H_RATIO = 0.75f;
constexpr float ICON_W_RATIO = 0.70f;
constexpr float LABEL_NO_ICON_VBUDGET_RATIO = 0.48f;
constexpr float LABEL_WITH_ICON_VBUDGET_RATIO = 0.20f;
constexpr float LABEL_ICON_FRACTION = 0.40f;
constexpr float LABEL_W_RATIO = 0.22f;
constexpr int16_t LABEL_FONT_MIN = 8;
constexpr int16_t LABEL_BUDGET_PAD = 12;

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

const lv_font_t *selectButtonFontFromTarget(int16_t targetPx) {
    if (targetPx >= 15)
        return FontManager::label(16);
    if (targetPx >= 13)
        return FontManager::label(14);
    return FontManager::label(12);
}

static constexpr int16_t MAP_BADGE_DIAMETER = 7;
static constexpr uint32_t MAP_BADGE_COLOR = 0x33CC44;

constexpr lv_opa_t BUTTON_BG_OPA_IDLE = LV_OPA_10;
constexpr lv_opa_t BUTTON_BG_OPA_ACTIVE = 0x55;
constexpr lv_opa_t BUTTON_ICON_OPA = 0xCC;
constexpr int16_t BUTTON_BORDER_WIDTH = 1;

struct ButtonTag {
    const CfgWidget *cfg;
    const CfgButtonParams *params;
    lv_obj_t *iconImg;
    lv_obj_t *labelObj;
    bool toggleActive;
    char lvglPath[LVGL_PATH_LEN];
    lv_obj_t *activeBadge;
    uint8_t mapSwitchIndex;
    bool hasMapSwitch;
    char signalId[CFG_MAX_SIGNAL_LEN];
    uint32_t signalSyncIgnoreUntilMs;
    uint8_t cycleIndex;
};

struct IconSource {
    const lv_img_dsc_t *dsc;
    const char *path;
};

IconSource resolveIconAsset(const CfgButtonParams &p, char *pathBuf, size_t pathBufLen) {
    pathBuf[0] = '\0';
    IconSource result{nullptr, pathBuf};
    if (p.iconPath[0] != '\0') {
        const char *prefix = (p.iconPath[0] == '/') ? "" : "/";
        snprintf(pathBuf, pathBufLen, "S:%s%s", prefix, p.iconPath);
        if (!IconAssets::exists(pathBuf))
            pathBuf[0] = '\0';
        return result;
    }
    result.dsc = IconAssetsBaked::resolve(p.iconName);
    if (result.dsc != nullptr)
        return result;
    const char *spiffs = IconAssets::path(p.iconName);
    if (spiffs[0] != '\0')
        strlcpy(pathBuf, spiffs, pathBufLen);
    return result;
}

constexpr uint32_t TOGGLE_DERIVED_ACTIVE_DELTA = 0x40;

uint32_t lightenRgb(uint32_t rgb, uint32_t delta) {
    const uint32_t r = (rgb >> 16) & 0xFF;
    const uint32_t g = (rgb >> 8) & 0xFF;
    const uint32_t b = rgb & 0xFF;
    const uint32_t rL = r + delta > 0xFF ? 0xFF : r + delta;
    const uint32_t gL = g + delta > 0xFF ? 0xFF : g + delta;
    const uint32_t bL = b + delta > 0xFF ? 0xFF : b + delta;
    return (rL << 16) | (gL << 8) | bL;
}

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

uint32_t cycleNormalColor(const CfgWidget &cfg, const CfgButtonParams &p, const CfgButtonState &s) {
    if (s.hasColors)
        return s.colorNormal.rgb;
    if (p.hasColors)
        return p.colorNormal.rgb;
    return cfg.style.primaryColor.rgb;
}

uint32_t cycleActiveColor(const CfgWidget &cfg, const CfgButtonParams &p, const CfgButtonState &s) {
    if (s.hasColors)
        return s.colorActive.rgb;
    if (p.hasColors)
        return p.colorActive.rgb;
    return lightenRgb(cfg.style.primaryColor.rgb, TOGGLE_DERIVED_ACTIVE_DELTA);
}

void applyCycleVisualState(lv_obj_t *btn, const ButtonTag &tag) {
    if (!tag.cfg || !tag.params || tag.params->mode != CfgButtonMode::CYCLE)
        return;
    if (tag.cycleIndex >= tag.params->statesCount)
        return;
    const CfgButtonState &state = tag.params->states[tag.cycleIndex];
    const uint32_t normalRgb = cycleNormalColor(*tag.cfg, *tag.params, state);
    const uint32_t activeRgb = cycleActiveColor(*tag.cfg, *tag.params, state);

    ButtonVisual idle;
    idle.bgColor = normalRgb;
    idle.bgOpa = BUTTON_BG_OPA_IDLE;
    idle.borderColor = tag.cfg->style.secondaryColor.rgb;
    idle.textColor = tag.cfg->style.textColor.rgb;
    applyButtonVisual(btn, tag, idle);

    lv_obj_set_style_bg_color(btn, lv_color_hex(activeRgb), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, BUTTON_BG_OPA_ACTIVE, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(activeRgb), LV_PART_MAIN | LV_STATE_PRESSED);

    if (tag.labelObj) {
        const char *labelText = state.label[0] != '\0' ? state.label : tag.params->label;
        lv_label_set_text(tag.labelObj, labelText);
    }
}

void btnClickHandler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;

    auto *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    auto *tag = static_cast<ButtonTag *>(lv_obj_get_user_data(btn));
    if (!tag || !tag->params)
        return;

    const char *btnId = tag->cfg ? tag->cfg->id : "?";

    if (tag->params->mode == CfgButtonMode::CYCLE) {
        if (tag->params->statesCount == 0) {
            LOG_DEBUG("BTN", "click id=%s mode=cycle dropped (statesCount=0)", btnId);
            return;
        }
        const uint8_t oldIdx = tag->cycleIndex;
        tag->cycleIndex = static_cast<uint8_t>((tag->cycleIndex + 1) % tag->params->statesCount);
        LOG_DEBUG("BTN", "click id=%s mode=cycle %u->%u action.type=%u", btnId,
                  static_cast<unsigned>(oldIdx), static_cast<unsigned>(tag->cycleIndex),
                  static_cast<unsigned>(tag->params->states[tag->cycleIndex].action.type));
        applyCycleVisualState(btn, *tag);
        ActionDispatcher::dispatchAction(tag->params->states[tag->cycleIndex].action, true);
        return;
    }

    if (tag->params->isToggle) {
        tag->toggleActive = !tag->toggleActive;
        LOG_DEBUG("BTN", "click id=%s mode=toggle visual=%s signal=%s", btnId,
                  tag->toggleActive ? "on" : "off",
                  tag->signalId[0] != '\0' ? tag->signalId : "(none)");
        applyToggleVisualState(btn, *tag);
        tag->signalSyncIgnoreUntilMs = millis() + BUTTON_SIGNAL_SYNC_GRACE_MS;
    } else {
        LOG_DEBUG("BTN", "click id=%s mode=single actions=%u", btnId,
                  static_cast<unsigned>(tag->params->actionsCount));
    }

    for (uint8_t i = 0; i < tag->params->actionsCount; ++i) {
        ActionDispatcher::dispatchAction(tag->params->actions[i], tag->toggleActive);
    }
}

} // namespace

lv_obj_t *ButtonWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *btn = lv_btn_create(parent);
    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(btn, px, py);
    lv_obj_set_size(btn, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));

    const CfgButtonParams &p = cfg.button;

    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, BUTTON_PAD_X, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, BUTTON_PAD_Y, LV_PART_MAIN);

    const bool hasIcon = p.showIcon && (p.iconPath[0] != '\0' || p.iconName[0] != '\0');
    const bool hasLabel = p.showLabel && p.label[0] != '\0';

    if (hasIcon || hasLabel) {
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(btn, BUTTON_ROW_GAP, LV_PART_MAIN);
    }

    WidgetTagPool::Slot<ButtonTag> tagSlot;
    ButtonTag *tag = tagSlot.get();
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
    tag->hasMapSwitch = false;
    strlcpy(tag->signalId, cfg.signalId, sizeof(tag->signalId));
    tag->signalSyncIgnoreUntilMs = 0;
    tag->cycleIndex = p.mode == CfgButtonMode::CYCLE ? p.initialActiveIndex : 0;

    if (p.mode == CfgButtonMode::SINGLE) {
        for (uint8_t i = 0; i < p.actionsCount; ++i) {
            if (p.actions[i].type == CfgButtonActionType::MAP_SWITCH) {
                tag->mapSwitchIndex = p.actions[i].mapIndex;
                tag->hasMapSwitch = true;
                break;
            }
        }
    }

    const int16_t targetIconSize = hasIcon ? computeIconSize(cfg.layout.w, cfg.layout.h) : 0;
    const int16_t targetFontSize =
        computeLabelFontSize(cfg.layout.w, cfg.layout.h, hasIcon, targetIconSize);
    const lv_font_t *btnFont = selectButtonFontFromTarget(targetFontSize);

    if (hasIcon) {
        const IconSource icon = resolveIconAsset(p, tag->lvglPath, sizeof(tag->lvglPath));
        const bool heapOk = icon.dsc != nullptr || heap_caps_get_largest_free_block(
                                                       MALLOC_CAP_8BIT) >= LVGL_FS_MIN_HEAP_BYTES;
        const bool hasSrc = icon.dsc != nullptr || tag->lvglPath[0] != '\0';
        if (hasSrc && heapOk) {
            tag->iconImg = lv_img_create(btn);
            if (icon.dsc != nullptr) {
                lv_img_set_src(tag->iconImg, icon.dsc);
            } else {
                lv_img_set_src(tag->iconImg, tag->lvglPath);
            }
            (void)targetIconSize;
        } else if (hasSrc && !heapOk) {
            LOG_WARN("BTN", "skipping icon %s — largest=%u below threshold", tag->lvglPath,
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
            tag->lvglPath[0] = '\0';
        }
    }

    if (hasLabel) {
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, p.label);
        lv_obj_set_style_text_font(label, btnFont, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        tag->labelObj = label;
    }

    {
        const ButtonVisual idle = computeButtonVisual(cfg, p, false);
        applyButtonVisual(btn, *tag, idle);
        if (tag->iconImg) {
            lv_obj_set_style_img_recolor(tag->iconImg, lv_color_hex(idle.textColor), 0);
            lv_obj_set_style_img_recolor_opa(tag->iconImg, BUTTON_ICON_OPA, 0);
        }
        if (!p.isToggle) {
            const ButtonVisual pressed = computeButtonVisual(cfg, p, true);
            lv_obj_set_style_bg_color(btn, lv_color_hex(pressed.bgColor),
                                      LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(btn, pressed.bgOpa, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_border_color(btn, lv_color_hex(pressed.borderColor),
                                          LV_PART_MAIN | LV_STATE_PRESSED);
        }
    }

    if (p.mode == CfgButtonMode::CYCLE) {
        applyCycleVisualState(btn, *tag);
    }

    if (tag->hasMapSwitch) {
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
    WidgetHelpers::attachTagDeleter(btn, tagSlot.commit());

    return btn;
}

void ButtonWidget::update(lv_obj_t *btn) {
    auto *tag = static_cast<ButtonTag *>(lv_obj_get_user_data(btn));
    if (!tag)
        return;

    const char *btnId = tag->cfg ? tag->cfg->id : "?";

    const int32_t graceDelta = static_cast<int32_t>(millis() - tag->signalSyncIgnoreUntilMs);
    if (tag->params && tag->params->isToggle && tag->signalId[0] != '\0' && graceDelta >= 0) {
        const SignalId sid = signalIdFromName(tag->signalId);
        const bool sigValid = sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid);
        const bool desiredActive = sigValid && SignalStore::read(sid, 0.0f) != 0.0f;
        if (desiredActive != tag->toggleActive) {
            LOG_DEBUG("BTN", "sync id=%s toggle %s->%s (signal=%s valid=%d)", btnId,
                      tag->toggleActive ? "on" : "off", desiredActive ? "on" : "off", tag->signalId,
                      sigValid ? 1 : 0);
            tag->toggleActive = desiredActive;
            applyToggleVisualState(btn, *tag);
        }
    }

    if (!tag->activeBadge || !tag->hasMapSwitch)
        return;

    const bool active =
        SignalStore::isValid(SignalIds::MAP_NUMBER) &&
        static_cast<uint8_t>(SignalStore::read(SignalIds::MAP_NUMBER)) == tag->mapSwitchIndex;

    const bool wasHidden = lv_obj_has_flag(tag->activeBadge, LV_OBJ_FLAG_HIDDEN);
    if (active && wasHidden) {
        LOG_DEBUG("BTN", "badge SHOW id=%s mapIdx=%u", btnId,
                  static_cast<unsigned>(tag->mapSwitchIndex));
        lv_obj_clear_flag(tag->activeBadge, LV_OBJ_FLAG_HIDDEN);
    } else if (!active && !wasHidden) {
        LOG_DEBUG("BTN", "badge HIDE id=%s mapIdx=%u", btnId,
                  static_cast<unsigned>(tag->mapSwitchIndex));
        lv_obj_add_flag(tag->activeBadge, LV_OBJ_FLAG_HIDDEN);
    }
}
