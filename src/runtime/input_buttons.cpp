#include "input_buttons.h"

#include "action_dispatcher.h"
#include "app_config.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "runtime/signal_store.h"
#include "runtime/task_spawn.h"
#include "ui/alert_takeover.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace InputButtons {

namespace {

constexpr uint32_t kInputPollPeriodMs = 1;
constexpr uint32_t kLongPressMs = 600;
constexpr uint32_t kDoubleTapGapMs = 250;

struct BindingState {
    bool rawPrev;
    bool debouncedPressed;
    uint32_t lastEdgeMs;
    uint32_t pressStartMs;
    uint32_t lastReleaseMs;
    bool longFiredThisPress;
    bool pendingDoubleArmed;
};

BindingState s_states[kCfgMaxInputBindings];
bool s_initDone = false;

bool isPressedNow(const CfgInputBinding &b) {
    const int raw = digitalRead(b.pin);
    if (b.active == CfgInputActive::ACTIVE_LOW) {
        return raw == LOW;
    }
    return raw == HIGH;
}

void syncSharedSignal(const CfgInputBinding &b) {
    if (b.signal[0] == '\0')
        return;
    const SignalId sid = signalIdFromName(b.signal);
    if (sid >= SignalIds::kSignalCount)
        return;
    const float current = SignalStore::read(sid, 0.0f);
    SignalStore::update(sid, current != 0.0f ? 0.0f : 1.0f);
}

void firePress(const CfgInputBinding &b, CfgInputPressKind detected) {
    if (b.kind != detected)
        return;
    if (AlertTakeover::isActive()) {
        LOG_INFO("INPUT", "binding=%s consumed as alert acknowledge", b.id);
        AlertTakeover::requestAcknowledge();
        return;
    }
    LOG_INFO("INPUT", "binding=%s pin=%d kind=%d → action", b.id, b.pin,
             static_cast<int>(detected));

    ActionDispatcher::dispatchAction(b.action, true);
    syncSharedSignal(b);
}

enum class Edge : uint8_t { Pressed, Released, Holding, Idle };

using EdgeHandler = void (*)(const CfgInputBinding &b, BindingState &s, uint32_t nowMs);

Edge edgeOf(bool raw, bool held) {
    if (raw == held)
        return raw ? Edge::Holding : Edge::Idle;
    return raw ? Edge::Pressed : Edge::Released;
}

void onPressed(const CfgInputBinding &, BindingState &s, uint32_t nowMs) {
    s.debouncedPressed = true;
    s.pressStartMs = nowMs;
    s.longFiredThisPress = false;
}

void registerTap(const CfgInputBinding &b, BindingState &s, uint32_t nowMs) {
    if (s.pendingDoubleArmed && nowMs - s.lastReleaseMs <= kDoubleTapGapMs) {
        firePress(b, CfgInputPressKind::DOUBLE);
        s.pendingDoubleArmed = false;
        return;
    }
    s.pendingDoubleArmed = true;
}

void onReleased(const CfgInputBinding &b, BindingState &s, uint32_t nowMs) {
    s.debouncedPressed = false;
    if (nowMs - s.pressStartMs < kLongPressMs)
        registerTap(b, s, nowMs);
    s.lastReleaseMs = nowMs;
}

void onHolding(const CfgInputBinding &b, BindingState &s, uint32_t nowMs) {
    if (s.longFiredThisPress || nowMs - s.pressStartMs < kLongPressMs)
        return;
    firePress(b, CfgInputPressKind::LONG);
    s.longFiredThisPress = true;
    s.pendingDoubleArmed = false;
}

void onIdle(const CfgInputBinding &, BindingState &, uint32_t) {}

constexpr EdgeHandler kEdgeHandlers[] = {onPressed, onReleased, onHolding, onIdle};

void flushPendingShortPress(const CfgInputBinding &b, BindingState &s, uint32_t nowMs) {
    if (!s.pendingDoubleArmed || s.debouncedPressed || nowMs - s.lastReleaseMs <= kDoubleTapGapMs)
        return;
    firePress(b, CfgInputPressKind::SHORT);
    s.pendingDoubleArmed = false;
}

void scanBinding(size_t idx, const CfgInputBinding &b, uint32_t nowMs) {
    BindingState &s = s_states[idx];
    const bool raw = isPressedNow(b);
    if (raw != s.rawPrev) {
        s.rawPrev = raw;
        s.lastEdgeMs = nowMs;
    }
    if (nowMs - s.lastEdgeMs < b.debounceMs)
        return;
    kEdgeHandlers[static_cast<uint8_t>(edgeOf(raw, s.debouncedPressed))](b, s, nowMs);
    flushPendingShortPress(b, s, nowMs);
}

void taskInput(void *) {
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(kInputPollPeriodMs);
    const CfgInputBindings &cfg = ConfigLoader::getInputBindings();

    while (true) {
        esp_task_wdt_reset();
        const uint32_t nowMs = millis();
        for (size_t i = 0; i < cfg.count && i < kCfgMaxInputBindings; ++i) {
            const CfgInputBinding &b = cfg.bindings[i];
            if (b.pin < 0)
                continue;
            scanBinding(i, b, nowMs);
        }
        vTaskDelayUntil(&lastWake, period);
    }
}

void configurePin(const CfgInputBinding &b) {
    if (b.pin < 0)
        return;
    const bool needsPullup = (b.pullup && b.active == CfgInputActive::ACTIVE_LOW);
    pinMode(b.pin, needsPullup ? INPUT_PULLUP : INPUT);
}

}

void init() {
    if (s_initDone)
        return;

    const CfgInputBindings &cfg = ConfigLoader::getInputBindings();
    if (!cfg.loaded || cfg.count == 0) {
        LOG_INFO("INPUT", "no input bindings configured — task not started");
        s_initDone = true;
        return;
    }

    memset(s_states, 0, sizeof(s_states));
    for (size_t i = 0; i < cfg.count && i < kCfgMaxInputBindings; ++i) {
        configurePin(cfg.bindings[i]);
        LOG_INFO("INPUT", "registered binding %s on GPIO%d (debounce=%ums)", cfg.bindings[i].id,
                 cfg.bindings[i].pin, static_cast<unsigned>(cfg.bindings[i].debounceMs));
    }

    if (TaskSpawn::spawnDynamic("input", taskInput, TASK_STACK_INPUT, kTaskPrioInput,
                                kTaskCoreInput) == nullptr) {
        LOG_ERROR("INPUT", "input task not started — bindings inert");
    }
    s_initDone = true;
}

}
