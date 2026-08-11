#include "button_widget.h"
#include "config/config_loader.h"
#include "runtime/action_dispatcher.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "ui/font_manager.h"
#include "ui/widget_label.h"
#include "ui/screen_profile.h"
#include "ui/theme_manager.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"
#include "layout_scale.h"
#include "diag/logger.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int16_t BUTTON_PAD_X = 7;
constexpr int16_t BUTTON_PAD_Y = 6;
constexpr int16_t BUTTON_ROW_GAP = 2;
constexpr int16_t BUTTON_KICKER_BAND_PX = 24;
constexpr float LABEL_GLYPH_WIDTH_RATIO = 0.65f;
constexpr int16_t LABEL_FONT_MIN = 8;
constexpr int16_t LABEL_BUDGET_PAD = 14;

constexpr int16_t BUTTON_FONT_TARGET_XL = 27;
constexpr int16_t BUTTON_FONT_TARGET_LG = 15;
constexpr int16_t BUTTON_FONT_TARGET_MD = 13;
constexpr uint8_t BUTTON_FONT_SIZE_XL = 28;
constexpr uint8_t BUTTON_FONT_SIZE_LG = 16;
constexpr uint8_t BUTTON_FONT_SIZE_MD = 14;
constexpr uint8_t BUTTON_FONT_SIZE_SM = 12;
constexpr uint8_t BUTTON_KICKER_FONT_PX = 10;
constexpr int16_t BUTTON_KICKER_TRACKING_PX = 2;
constexpr lv_opa_t BUTTON_KICKER_ENGAGED_OPA = 0xC0;

int16_t computeLabelFontSize(int16_t w, int16_t h, bool hasKicker, size_t labelLen) {
    const float verticalBudget =
        static_cast<float>(h - (hasKicker ? BUTTON_KICKER_BAND_PX : 2 * BUTTON_PAD_Y));
    const float labelBudget = static_cast<float>(w - LABEL_BUDGET_PAD);
    const float glyphs = static_cast<float>(labelLen > 0 ? labelLen : 1);
    const float widthCap = labelBudget / (glyphs * LABEL_GLYPH_WIDTH_RATIO);
    float fontSize = fminf(verticalBudget, widthCap);
    if (fontSize < LABEL_FONT_MIN)
        fontSize = LABEL_FONT_MIN;
    return static_cast<int16_t>(fontSize);
}

const lv_font_t *selectButtonFontFromTarget(int16_t targetPx) {
    if (targetPx >= BUTTON_FONT_TARGET_XL)
        return FontManager::label(BUTTON_FONT_SIZE_XL);
    if (targetPx >= BUTTON_FONT_TARGET_LG)
        return FontManager::label(BUTTON_FONT_SIZE_LG);
    if (targetPx >= BUTTON_FONT_TARGET_MD)
        return FontManager::label(BUTTON_FONT_SIZE_MD);
    return FontManager::label(BUTTON_FONT_SIZE_SM);
}

static constexpr int16_t MAP_BADGE_DIAMETER = 12;
static constexpr uint32_t MAP_BADGE_COLOR = WidgetHelpers::kAccentRgb;

constexpr lv_opa_t BUTTON_BG_OPA_IDLE = LV_OPA_TRANSP;
constexpr lv_opa_t BUTTON_BG_OPA_ACTIVE = LV_OPA_COVER;
constexpr int16_t BUTTON_BORDER_WIDTH = 2;
constexpr uint32_t BUTTON_ACTIVE_TEXT_RGB = 0xFFFFFF;
constexpr int16_t BUTTON_MIN_TOUCH_W = 48;
constexpr int16_t BUTTON_MIN_TOUCH_H = 50;

struct ButtonTag {
    const CfgWidget *cfg;
    const CfgButtonParams *params;
    lv_obj_t *labelObj;
    lv_obj_t *kickerObj;
    bool toggleActive;
    lv_obj_t *activeBadge;
    uint8_t mapSwitchIndex;
    bool hasMapSwitch;
    char signalId[CFG_MAX_SIGNAL_LEN];
    uint32_t signalSyncIgnoreUntilMs;
    uint8_t cycleIndex;
};

void setUppercased(lv_obj_t *label, const char *text) {
    char upper[CFG_MAX_NAME_LEN];
    size_t i = 0;
    for (; i < sizeof(upper) - 1 && text[i] != '\0'; ++i)
        upper[i] =
            (text[i] >= 'a' && text[i] <= 'z') ? static_cast<char>(text[i] - 'a' + 'A') : text[i];
    upper[i] = '\0';
    lv_label_set_text(label, upper);
}

struct ButtonVisual {
    uint32_t bgColor;
    lv_opa_t bgOpa;
    uint32_t borderColor;
    uint32_t textColor;
};

ButtonVisual computeButtonVisual(const CfgWidget &cfg, const CfgButtonParams &p, bool active) {
    const uint32_t normalColor = p.hasColors ? p.colorNormal.rgb : cfg.style.primaryColor.rgb;
    const uint32_t activeColor = p.hasColors ? p.colorActive.rgb : WidgetHelpers::kAccentRgb;
    ButtonVisual v;
    if (active) {
        v.bgColor = activeColor;
        v.bgOpa = BUTTON_BG_OPA_ACTIVE;
        v.borderColor = activeColor;
        v.textColor = BUTTON_ACTIVE_TEXT_RGB;
    } else {
        v.bgColor = normalColor;
        v.bgOpa = BUTTON_BG_OPA_IDLE;
        v.borderColor =
            ThemeManager::getEffectiveTextColor(cfg.style.textColor.rgb, cfg.style.respectDayMode);
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
    if (tag.kickerObj) {
        const bool engaged = v.bgOpa == BUTTON_BG_OPA_ACTIVE;
        lv_obj_set_style_text_color(
            tag.kickerObj, lv_color_hex(engaged ? 0xFFFFFFu : WidgetHelpers::kMutedRgb), 0);
        lv_obj_set_style_text_opa(tag.kickerObj, engaged ? BUTTON_KICKER_ENGAGED_OPA : LV_OPA_COVER,
                                  0);
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
    (void)cfg;
    return WidgetHelpers::kAccentRgb;
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
    idle.borderColor = ThemeManager::getEffectiveTextColor(tag.cfg->style.textColor.rgb,
                                                           tag.cfg->style.respectDayMode);
    idle.textColor = tag.cfg->style.textColor.rgb;
    applyButtonVisual(btn, tag, idle);

    lv_obj_set_style_bg_color(btn, lv_color_hex(activeRgb), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, BUTTON_BG_OPA_ACTIVE, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(activeRgb), LV_PART_MAIN | LV_STATE_PRESSED);

    if (tag.labelObj) {
        const char *labelText = state.label[0] != '\0' ? state.label : tag.params->label;
        setUppercased(tag.labelObj, labelText);
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

void applyButtonTouchPadding(lv_obj_t *btn, int16_t scaledW, int16_t scaledH) {
    const int16_t minTouchW = LayoutScale::x(BUTTON_MIN_TOUCH_W);
    const int16_t minTouchH = LayoutScale::y(BUTTON_MIN_TOUCH_H);
    const int16_t padX =
        scaledW < minTouchW ? static_cast<int16_t>((minTouchW - scaledW + 1) / 2) : 0;
    const int16_t padY =
        scaledH < minTouchH ? static_cast<int16_t>((minTouchH - scaledH + 1) / 2) : 0;
    if (padX > 0 || padY > 0)
        lv_obj_set_ext_click_area(btn, padX > padY ? padX : padY);
}

void initButtonTag(ButtonTag *tag, const CfgWidget &cfg, const CfgButtonParams &p) {
    tag->cfg = &cfg;
    tag->params = &p;
    tag->labelObj = nullptr;
    tag->kickerObj = nullptr;
    tag->toggleActive = false;
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
}

void createButtonLabel(lv_obj_t *btn, ButtonTag *tag, const CfgButtonParams &p,
                       const lv_font_t *btnFont) {
    lv_obj_t *label = lv_label_create(btn);
    setUppercased(label, p.label);
    lv_obj_set_style_text_font(label, btnFont, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    tag->labelObj = label;
}

const char *kickerFromAction(const CfgButtonParams &p) {
    if (p.actionsCount == 0 && p.mode != CfgButtonMode::CYCLE)
        return nullptr;
    const CfgButtonActionType type =
        p.mode == CfgButtonMode::CYCLE
            ? (p.statesCount > 0 ? p.states[0].action.type : CfgButtonActionType::UNKNOWN)
            : p.actions[0].type;
    switch (type) {
        case CfgButtonActionType::NAV_PAGE:
            return "PAGE";
        case CfgButtonActionType::MAP_SWITCH:
            return "MAP";
        case CfgButtonActionType::CRUISE_CONTROL:
            return "CRUISE";
        case CfgButtonActionType::CAN_RAW:
        case CfgButtonActionType::UNKNOWN:
        default:
            return nullptr;
    }
}

void createButtonKicker(lv_obj_t *btn, ButtonTag *tag, const CfgWidget &cfg) {
    const char *text = cfg.button && cfg.button->kicker[0] != '\0' ? cfg.button->kicker : nullptr;
    if (!text)
        text = WidgetLabelOverlay::displayLabelForSignal(cfg.signalId);
    if (!text && cfg.button)
        text = kickerFromAction(*cfg.button);
    if (!text)
        return;
    lv_obj_t *kicker = lv_label_create(btn);
    if (!kicker)
        return;
    lv_label_set_text(kicker, text);
    lv_obj_set_style_text_font(kicker, FontManager::label(BUTTON_KICKER_FONT_PX), 0);
    lv_obj_set_style_text_letter_space(kicker, BUTTON_KICKER_TRACKING_PX, 0);
    lv_obj_set_style_text_color(kicker, lv_color_hex(WidgetHelpers::kMutedRgb), 0);
    lv_obj_set_style_text_align(kicker, LV_TEXT_ALIGN_LEFT, 0);
    tag->kickerObj = kicker;
}

void applyButtonInitialVisual(lv_obj_t *btn, ButtonTag *tag, const CfgWidget &cfg,
                              const CfgButtonParams &p) {
    const ButtonVisual idle = computeButtonVisual(cfg, p, false);
    applyButtonVisual(btn, *tag, idle);
    if (!p.isToggle) {
        const ButtonVisual pressed = computeButtonVisual(cfg, p, true);
        lv_obj_set_style_bg_color(btn, lv_color_hex(pressed.bgColor),
                                  LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, pressed.bgOpa, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, lv_color_hex(pressed.borderColor),
                                      LV_PART_MAIN | LV_STATE_PRESSED);
    }
    if (tag->labelObj) {
        lv_obj_set_style_text_color(tag->labelObj, lv_color_hex(BUTTON_ACTIVE_TEXT_RGB),
                                    LV_PART_MAIN | LV_STATE_PRESSED);
    }
}

void createButtonMapBadge(lv_obj_t *btn, ButtonTag *tag) {
    lv_obj_t *badge = WidgetHelpers::makeCircleBadge(btn, LayoutScale::square(MAP_BADGE_DIAMETER),
                                                     MAP_BADGE_COLOR);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_HIDDEN);
    tag->activeBadge = badge;
}

} // namespace

lv_obj_t *ButtonWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    lv_obj_t *btn = lv_btn_create(parent);
    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(btn, px, py);
    lv_obj_set_size(btn, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));
    const int16_t scaledW = ScreenProfile::scaleXVal(cfg.layout.w);
    const int16_t scaledH = ScreenProfile::scaleYVal(cfg.layout.h);
    applyButtonTouchPadding(btn, scaledW, scaledH);

    if (!cfg.button) {
        LOG_WARN("WF", "button '%s' has no params — skipped", cfg.id);
        lv_obj_del(btn);
        return nullptr;
    }
    const CfgButtonParams &p = *cfg.button;

    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(btn, LayoutScale::x(BUTTON_PAD_X), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(btn, LayoutScale::y(BUTTON_PAD_Y), LV_PART_MAIN);

    const bool hasLabel = p.showLabel && p.label[0] != '\0';

    if (hasLabel) {
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(btn, LayoutScale::y(BUTTON_ROW_GAP), LV_PART_MAIN);
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
    initButtonTag(tag, cfg, p);

    if (hasLabel)
        createButtonKicker(btn, tag, cfg);

    const int16_t targetFontSize = computeLabelFontSize(cfg.layout.w, cfg.layout.h,
                                                        tag->kickerObj != nullptr, strlen(p.label));
    const lv_font_t *btnFont = selectButtonFontFromTarget(targetFontSize);

    if (hasLabel)
        createButtonLabel(btn, tag, p, btnFont);

    applyButtonInitialVisual(btn, tag, cfg, p);

    if (p.mode == CfgButtonMode::CYCLE)
        applyCycleVisualState(btn, *tag);

    if (tag->hasMapSwitch)
        createButtonMapBadge(btn, tag);

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
