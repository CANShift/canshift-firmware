#include "button_widget.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "control_state_rs.h"
#include "diag/logger.h"
#include "layout_scale.h"
#include "runtime/action_dispatcher.h"
#include "runtime/signal_store.h"
#include "ui/control_splash.h"
#include "ui/control_status.h"
#include "ui/control_vocabulary.h"
#include "ui/screen_profile.h"
#include "ui/signal_presentation.h"
#include "ui/widget_label.h"
#include "ui/widgets/control_button.h"
#include "ui/widgets/widget_helpers.h"
#include "ui/widgets/widget_tag_pool.h"

#include <Arduino.h>
#include <lvgl.h>
#include <string.h>

namespace {

using ControlVocabulary::Control;
using ControlVocabulary::ControlKind;
using ControlVocabulary::ControlState;

constexpr int16_t kMinTouchW = 48;
constexpr int16_t kMinTouchH = 50;
constexpr size_t kTextLen = 48;

struct ButtonTag {
    const CfgWidget *cfg;
    const CfgButtonParams *params;
    const Control *control;
    ControlButton::Surface surface;
    ControlStepperRs stepper;
    bool engaged;
    uint8_t cycleIndex;
    uint32_t signalSyncIgnoreUntilMs;
    uint32_t commandDeadlineMs;
    uint8_t syncedLevel;
    bool hasSyncedLevel;
    char signalId[CFG_MAX_SIGNAL_LEN];
};

void setUppercased(char *out, size_t outLen, const char *text) {
    size_t i = 0;
    for (; i + 1 < outLen && text[i] != '\0'; ++i)
        out[i] =
            (text[i] >= 'a' && text[i] <= 'z') ? static_cast<char>(text[i] - 'a' + 'A') : text[i];
    out[i] = '\0';
}

struct ActionKicker {
    CfgButtonActionType type;
    const char *kicker;
};

constexpr ActionKicker kActionKickers[] = {{CfgButtonActionType::NAV_PAGE, "PAGE"},
                                           {CfgButtonActionType::MAP_SWITCH, "MAP"},
                                           {CfgButtonActionType::CRUISE_CONTROL, "CRUISE"}};

CfgButtonActionType firstActionType(const CfgButtonParams &p) {
    if (p.mode == CfgButtonMode::CYCLE)
        return p.statesCount > 0 ? p.states[0].action.type : CfgButtonActionType::UNKNOWN;
    return p.actionsCount > 0 ? p.actions[0].type : CfgButtonActionType::UNKNOWN;
}

const char *kickerFromAction(const CfgButtonParams &p) {
    const CfgButtonActionType type = firstActionType(p);
    for (const ActionKicker &row : kActionKickers) {
        if (row.type == type)
            return row.kicker;
    }
    return "";
}

const char *fallbackKicker(const CfgWidget &cfg) {
    if (cfg.button && cfg.button->kicker[0] != '\0')
        return cfg.button->kicker;
    const char *fromSignal = SignalPresentation::kickerForSignal(cfg.signalId);
    if (fromSignal)
        return fromSignal;
    return cfg.button ? kickerFromAction(*cfg.button) : "";
}

const char *fallbackWord(const ButtonTag &tag) {
    if (!tag.params)
        return "";
    if (tag.params->mode == CfgButtonMode::CYCLE && tag.cycleIndex < tag.params->statesCount) {
        const CfgButtonState &state = tag.params->states[tag.cycleIndex];
        return state.label[0] != '\0' ? state.label : tag.params->label;
    }
    return tag.params->label;
}

ControlState resolveState(const ButtonTag &tag) {
    if (!tag.control)
        return tag.engaged ? ControlState::ACTIVE : ControlState::OFF;
    const ControlStatus::Request request = {tag.engaged, tag.stepper.level};
    return ControlStatus::evaluate(*tag.control, tag.signalId, request);
}

void refreshTexts(const ButtonTag &tag, ControlState state) {
    char kicker[kTextLen];
    char word[kTextLen];
    if (!tag.control) {
        setUppercased(kicker, sizeof(kicker), fallbackKicker(*tag.cfg));
        setUppercased(word, sizeof(word), fallbackWord(tag));
        ControlButton::setTexts(tag.surface, kicker, word);
        return;
    }
    const ControlVocabulary::Phrase &phrase = ControlVocabulary::phraseFor(*tag.control, state);
    const int param = ControlStatus::paramValue(phrase.param, tag.stepper.level);
    ControlVocabulary::composeKicker(*tag.control, state, param, kicker, sizeof(kicker));
    ControlVocabulary::composeStateWord(*tag.control, state, param, word, sizeof(word));
    ControlButton::setTexts(tag.surface, kicker, word);
}

ControlState applyState(ButtonTag &tag) {
    const ControlState state = resolveState(tag);
    refreshTexts(tag, state);
    ControlButton::paint(tag.surface, state, tag.stepper.level);
    return state;
}

void applyStateWithReceipt(ButtonTag &tag) {
    const ControlState state = applyState(tag);
    if (!tag.control)
        return;
    ControlSplash::raiseFor(*tag.control, state, tag.stepper.level);
}

bool isStepper(const ButtonTag &tag) {
    return tag.control && tag.control->kind == ControlKind::STEPPER;
}

void dispatchActions(const ButtonTag &tag, bool isActive) {
    if (!tag.params)
        return;
    for (uint8_t i = 0; i < tag.params->actionsCount; ++i) {
        CfgButtonAction action = tag.params->actions[i];
        if (action.type == CfgButtonActionType::MAP_SWITCH && isStepper(tag))
            action.mapIndex = tag.stepper.level;
        ActionDispatcher::dispatchAction(action, isActive);
    }
}

ButtonTag *tagOf(lv_event_t *e) {
    auto *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));
    return btn ? static_cast<ButtonTag *>(lv_obj_get_user_data(btn)) : nullptr;
}

void stepperPressCb(lv_event_t *e) {
    ButtonTag *tag = tagOf(e);
    if (!tag)
        return;
    control_stepper_press_rs(&tag->stepper, millis());
}

void stepperReleaseCb(lv_event_t *e) {
    ButtonTag *tag = tagOf(e);
    if (!tag)
        return;
    if (!control_stepper_release_rs(&tag->stepper, millis()))
        return;
    LOG_DEBUG("BTN", "step id=%s level=%u", tag->cfg ? tag->cfg->id : "?",
              static_cast<unsigned>(tag->stepper.level));
    dispatchActions(*tag, tag->stepper.level > 0);
    applyStateWithReceipt(*tag);
}

void cycleClickCb(lv_event_t *e) {
    ButtonTag *tag = tagOf(e);
    if (!tag || !tag->params || tag->params->statesCount == 0)
        return;
    tag->cycleIndex = static_cast<uint8_t>((tag->cycleIndex + 1) % tag->params->statesCount);
    ActionDispatcher::dispatchAction(tag->params->states[tag->cycleIndex].action, true);
    (void)applyState(*tag);
}

void toggleClickCb(lv_event_t *e) {
    ButtonTag *tag = tagOf(e);
    if (!tag || !tag->params)
        return;
    if (tag->params->isToggle || tag->control) {
        tag->engaged = !tag->engaged;
        tag->signalSyncIgnoreUntilMs = millis() + BUTTON_SIGNAL_SYNC_GRACE_MS;
        tag->commandDeadlineMs = tag->engaged ? millis() + BUTTON_COMMAND_TIMEOUT_MS : 0;
    }
    LOG_DEBUG("BTN", "tap id=%s engaged=%d", tag->cfg ? tag->cfg->id : "?", tag->engaged ? 1 : 0);
    dispatchActions(*tag, tag->engaged);
    applyStateWithReceipt(*tag);
}

void applyTouchPadding(lv_obj_t *btn, int16_t scaledW, int16_t scaledH) {
    const int16_t minW = LayoutScale::x(kMinTouchW);
    const int16_t minH = LayoutScale::y(kMinTouchH);
    const int16_t padX = scaledW < minW ? static_cast<int16_t>((minW - scaledW + 1) / 2) : 0;
    const int16_t padY = scaledH < minH ? static_cast<int16_t>((minH - scaledH + 1) / 2) : 0;
    if (padX > 0 || padY > 0)
        lv_obj_set_ext_click_area(btn, padX > padY ? padX : padY);
}

void initTag(ButtonTag *tag, const CfgWidget &cfg, const CfgButtonParams &p) {
    tag->cfg = &cfg;
    tag->params = &p;
    tag->control = ControlVocabulary::find(p.kicker);
    tag->surface = {};
    tag->engaged = false;
    tag->cycleIndex = p.mode == CfgButtonMode::CYCLE ? p.initialActiveIndex : 0;
    tag->signalSyncIgnoreUntilMs = 0;
    tag->commandDeadlineMs = 0;
    tag->syncedLevel = 0;
    tag->hasSyncedLevel = false;
    strlcpy(tag->signalId, cfg.signalId, sizeof(tag->signalId));
    const uint8_t floor = tag->control ? tag->control->stepFloor : 0;
    control_stepper_init_rs(&tag->stepper, floor);
}

void attachEvents(lv_obj_t *btn, const ButtonTag &tag) {
    if (isStepper(tag)) {
        lv_obj_add_event_cb(btn, stepperPressCb, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(btn, stepperReleaseCb, LV_EVENT_RELEASED, nullptr);
        lv_obj_add_event_cb(btn, stepperReleaseCb, LV_EVENT_PRESS_LOST, nullptr);
        return;
    }
    if (tag.params && tag.params->mode == CfgButtonMode::CYCLE) {
        lv_obj_add_event_cb(btn, cycleClickCb, LV_EVENT_CLICKED, nullptr);
        return;
    }
    lv_obj_add_event_cb(btn, toggleClickCb, LV_EVENT_CLICKED, nullptr);
}

void syncEngagedFromSignal(ButtonTag &tag) {
    if (tag.control || !tag.params || !tag.params->isToggle || tag.signalId[0] == '\0')
        return;
    if (static_cast<int32_t>(millis() - tag.signalSyncIgnoreUntilMs) < 0)
        return;
    const SignalId sid = signalIdFromName(tag.signalId);
    const bool valid = sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid);
    tag.engaged = valid && SignalStore::read(sid, 0.0f) != 0.0f;
}

void pollCommandTimeout(ButtonTag &tag) {
    if (!tag.control || ControlVocabulary::hasArmedState(*tag.control))
        return;
    if (!tag.engaged || tag.commandDeadlineMs == 0)
        return;
    if (ControlStatus::isConfirming(tag.signalId)) {
        tag.commandDeadlineMs = 0;
        return;
    }
    if (static_cast<int32_t>(millis() - tag.commandDeadlineMs) < 0)
        return;
    LOG_DEBUG("BTN", "timeout id=%s — no confirmation", tag.cfg ? tag.cfg->id : "?");
    tag.engaged = false;
    tag.commandDeadlineMs = 0;
}

void pollStepper(ButtonTag &tag) {
    if (!isStepper(tag))
        return;
    uint8_t level = 0;
    const bool fromSignal = ControlStatus::levelFromSignal(tag.control->id, &level);
    if (fromSignal && (!tag.hasSyncedLevel || level != tag.syncedLevel)) {
        tag.syncedLevel = level;
        tag.hasSyncedLevel = true;
        control_stepper_sync_rs(&tag.stepper, level);
    }
    if (!control_stepper_poll_rs(&tag.stepper, millis()))
        return;
    LOG_DEBUG("BTN", "hold id=%s level=%u", tag.cfg ? tag.cfg->id : "?",
              static_cast<unsigned>(tag.stepper.level));
    dispatchActions(tag, tag.stepper.level > 0);
    applyStateWithReceipt(tag);
}

} // namespace

lv_obj_t *ButtonWidget::create(lv_obj_t *parent, const CfgWidget &cfg, int16_t yOffset) {
    if (!cfg.button) {
        LOG_WARN("WF", "button '%s' has no params — skipped", cfg.id);
        return nullptr;
    }
    lv_obj_t *btn = lv_btn_create(parent);
    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    const int16_t scaledW = ScreenProfile::scaleXVal(cfg.layout.w);
    const int16_t scaledH = ScreenProfile::scaleYVal(cfg.layout.h);
    lv_obj_set_pos(btn, px, py);
    lv_obj_set_size(btn, scaledW, scaledH);
    applyTouchPadding(btn, scaledW, scaledH);

    WidgetTagPool::Slot<ButtonTag> tagSlot;
    ButtonTag *tag = WidgetHelpers::acquireTag(tagSlot, cfg.id, "BTN", btn);
    if (!tag)
        return nullptr;
    initTag(tag, cfg, *cfg.button);
    tag->surface = ControlButton::build(btn, isStepper(*tag));

    lv_obj_set_user_data(btn, tag);
    attachEvents(btn, *tag);
    (void)applyState(*tag);
    WidgetHelpers::attachTagDeleter(btn, tagSlot.commit());
    return btn;
}

void ButtonWidget::update(lv_obj_t *btn) {
    auto *tag = static_cast<ButtonTag *>(lv_obj_get_user_data(btn));
    if (!tag)
        return;
    pollStepper(*tag);
    syncEngagedFromSignal(*tag);
    pollCommandTimeout(*tag);
    (void)applyState(*tag);
}
