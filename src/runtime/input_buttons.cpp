#include "input_buttons.h"

#include "action_dispatcher.h"
#include "app_config.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "runtime/signal_store.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace InputButtons {

namespace {

constexpr uint32_t INPUT_POLL_PERIOD_MS = 1;
constexpr uint32_t LONG_PRESS_MS = 600;
constexpr uint32_t DOUBLE_TAP_GAP_MS = 250;

struct BindingState {
    bool rawPrev;
    bool debouncedPressed;
    uint32_t lastEdgeMs;
    uint32_t pressStartMs;
    uint32_t lastReleaseMs;
    bool longFiredThisPress;
    bool pendingDoubleArmed;
};

BindingState s_states[CFG_MAX_INPUT_BINDINGS];
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
    if (sid >= SignalIds::SIGNAL_COUNT)
        return;
    const float current = SignalStore::read(sid, 0.0f);
    SignalStore::set(sid, current != 0.0f ? 0.0f : 1.0f);
}

void firePress(const CfgInputBinding &b, CfgInputPressKind detected) {
    if (b.kind != detected)
        return;
    LOG_INFO("INPUT", "binding=%s pin=%d kind=%d → action", b.id, b.pin,
             static_cast<int>(detected));

    ActionDispatcher::dispatchAction(b.action, true);
    syncSharedSignal(b);
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

    if (raw && !s.debouncedPressed) {
        s.debouncedPressed = true;
        s.pressStartMs = nowMs;
        s.longFiredThisPress = false;
    } else if (!raw && s.debouncedPressed) {
        s.debouncedPressed = false;
        const uint32_t heldMs = nowMs - s.pressStartMs;
        const bool wasLong = (heldMs >= LONG_PRESS_MS);
        if (!wasLong && s.pendingDoubleArmed && nowMs - s.lastReleaseMs <= DOUBLE_TAP_GAP_MS) {
            firePress(b, CfgInputPressKind::DOUBLE);
            s.pendingDoubleArmed = false;
        } else if (!wasLong) {

            s.pendingDoubleArmed = true;
        }
        s.lastReleaseMs = nowMs;
    } else if (raw && s.debouncedPressed && !s.longFiredThisPress) {
        if (nowMs - s.pressStartMs >= LONG_PRESS_MS) {
            firePress(b, CfgInputPressKind::LONG);
            s.longFiredThisPress = true;
            s.pendingDoubleArmed = false;
        }
    }

    if (s.pendingDoubleArmed && !s.debouncedPressed &&
        nowMs - s.lastReleaseMs > DOUBLE_TAP_GAP_MS) {
        firePress(b, CfgInputPressKind::SHORT);
        s.pendingDoubleArmed = false;
    }
}

void taskInput(void *) {
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(INPUT_POLL_PERIOD_MS);
    const CfgInputBindings &cfg = ConfigLoader::getInputBindings();

    while (true) {
        esp_task_wdt_reset();
        const uint32_t nowMs = millis();
        for (size_t i = 0; i < cfg.count && i < CFG_MAX_INPUT_BINDINGS; ++i) {
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

} // namespace

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
    for (size_t i = 0; i < cfg.count && i < CFG_MAX_INPUT_BINDINGS; ++i) {
        configurePin(cfg.bindings[i]);
        LOG_INFO("INPUT", "registered binding %s on GPIO%d (debounce=%ums)", cfg.bindings[i].id,
                 cfg.bindings[i].pin, static_cast<unsigned>(cfg.bindings[i].debounceMs));
    }

    TaskHandle_t inputHandle = nullptr;
    const BaseType_t ok = xTaskCreatePinnedToCore(taskInput, "input", TASK_STACK_INPUT, nullptr,
                                                  TASK_PRIO_INPUT, &inputHandle, TASK_CORE_INPUT);
    if (ok != pdPASS) {
        LOG_ERROR("INPUT", "xTaskCreatePinnedToCore(input) failed");
    } else if (inputHandle) {
        const esp_err_t wdtErr = esp_task_wdt_add(inputHandle);
        if (wdtErr != ESP_OK)
            LOG_WARN("INPUT", "WDT add(input) failed: %d", static_cast<int>(wdtErr));
    }
    s_initDone = true;
}

} // namespace InputButtons
