// input_buttons.cpp — GPIO polling, debounce, press classification (#833).
//
// Polls every loaded input binding at INPUT_POLL_HZ from a dedicated FreeRTOS
// task pinned to core 0 (so it cannot starve the UI). Three press kinds are
// produced — SHORT / LONG / DOUBLE — but only the kind that matches the
// binding's `kind` field actually fires its action. That mirrors the issue
// scope (one action per binding) and keeps the dispatch contract identical
// to the on-screen button widget.
//
// Toggle sync (issue #833 user request): when the binding declares a
// `signal`, after dispatch we flip the matching SignalStore entry so any
// on-screen button widget bound to the same signal redraws on the next UI
// tick — no waiting on an ECU echo round-trip.

#include "input_buttons.h"

#include "action_dispatcher.h"
#include "app_config.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "runtime/signal_store.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

namespace InputButtons {

namespace {

// 1 kHz polling — the standard sweet-spot for debounced GPIO scanning. Cost
// at this rate with ≤16 bindings is well below 1% of a core.
constexpr uint32_t INPUT_POLL_PERIOD_MS = 1;

// Long-press: how long the button must stay pressed before we fire LONG.
constexpr uint32_t LONG_PRESS_MS = 600;

// Double-tap: max gap between releases.
constexpr uint32_t DOUBLE_TAP_GAP_MS = 250;

// Per-binding runtime state.
struct BindingState {
    bool rawPrev;            // Last sampled raw pin level
    bool debouncedPressed;   // Logical pressed state after debounce
    uint32_t lastEdgeMs;     // Tick of the last raw-level change
    uint32_t pressStartMs;   // Tick when debouncedPressed became true
    uint32_t lastReleaseMs;  // Tick of last debounced release (for double-tap)
    bool longFiredThisPress; // Guards against firing LONG twice during a hold
    bool pendingDoubleArmed; // True if a SHORT release is waiting for a second tap
};

BindingState s_states[CFG_MAX_INPUT_BINDINGS];
bool s_initDone = false;

// Read the configured logical "pressed" state from the raw digital level.
bool isPressedNow(const CfgInputBinding &b) {
    const int raw = digitalRead(b.pin);
    if (b.active == CfgInputActive::ACTIVE_LOW) {
        return raw == LOW;
    }
    return raw == HIGH;
}

// Push a synthetic signal update so dashboard button widgets bound to the
// same signal flip their visual toggle without waiting for the ECU echo.
// No-op when binding.signal is empty or names an unknown signal.
void syncSharedSignal(const CfgInputBinding &b) {
    if (b.signal[0] == '\0')
        return;
    const SignalId sid = signalIdFromName(b.signal);
    if (sid >= SignalIds::SIGNAL_COUNT)
        return;
    const float current = SignalStore::read(sid, 0.0f);
    SignalStore::update(sid, current != 0.0f ? 0.0f : 1.0f);
}

// Resolve press kind and (when it matches the binding) fire the action.
void firePress(const CfgInputBinding &b, CfgInputPressKind detected) {
    if (b.kind != detected)
        return;
    LOG_INFO("INPUT", "binding=%s pin=%d kind=%d → action", b.id, b.pin,
             static_cast<int>(detected));
    // Physical buttons always pass isActive=true. A toggle button widget
    // tracks its own latched state; physical inputs are momentary edges.
    ActionDispatcher::dispatchAction(b.action, true);
    syncSharedSignal(b);
}

// Apply debounce + classify press kind for one binding.
void scanBinding(size_t idx, const CfgInputBinding &b, uint32_t nowMs) {
    BindingState &s = s_states[idx];
    const bool raw = isPressedNow(b);

    if (raw != s.rawPrev) {
        s.rawPrev = raw;
        s.lastEdgeMs = nowMs;
    }
    // Wait until the line has been stable for `debounceMs` before promoting
    // the raw level into the debounced state.
    if (nowMs - s.lastEdgeMs < b.debounceMs)
        return;

    if (raw && !s.debouncedPressed) {
        // Press edge.
        s.debouncedPressed = true;
        s.pressStartMs = nowMs;
        s.longFiredThisPress = false;
    } else if (!raw && s.debouncedPressed) {
        // Release edge.
        s.debouncedPressed = false;
        const uint32_t heldMs = nowMs - s.pressStartMs;
        const bool wasLong = (heldMs >= LONG_PRESS_MS);
        // Double-tap detection: short release within DOUBLE_TAP_GAP_MS after
        // a previous short release.
        if (!wasLong && s.pendingDoubleArmed && nowMs - s.lastReleaseMs <= DOUBLE_TAP_GAP_MS) {
            firePress(b, CfgInputPressKind::DOUBLE);
            s.pendingDoubleArmed = false;
        } else if (!wasLong) {
            // Defer SHORT firing until either the double-tap window expires
            // or a second tap arrives. If this binding's `kind` isn't SHORT,
            // we still want to track double-tap state.
            s.pendingDoubleArmed = true;
        }
        s.lastReleaseMs = nowMs;
    } else if (raw && s.debouncedPressed && !s.longFiredThisPress) {
        // Mid-hold: fire LONG once the threshold is crossed.
        if (nowMs - s.pressStartMs >= LONG_PRESS_MS) {
            firePress(b, CfgInputPressKind::LONG);
            s.longFiredThisPress = true;
            // Cancel any pending SHORT/DOUBLE — this was a long press.
            s.pendingDoubleArmed = false;
        }
    }

    // Settle the pending SHORT once the double-tap window closes without a
    // second tap. We deliberately check AFTER edge handling so the SHORT
    // never races with a DOUBLE classification on the same tick.
    if (s.pendingDoubleArmed && !s.debouncedPressed &&
        nowMs - s.lastReleaseMs > DOUBLE_TAP_GAP_MS) {
        firePress(b, CfgInputPressKind::SHORT);
        s.pendingDoubleArmed = false;
    }
}

void taskInput(void *) {
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(INPUT_POLL_PERIOD_MS);

    while (true) {
        const CfgInputBindings &cfg = ConfigLoader::getInputBindings();
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

    const BaseType_t ok = xTaskCreatePinnedToCore(taskInput, "input", TASK_STACK_INPUT, nullptr,
                                                  TASK_PRIO_INPUT, nullptr, TASK_CORE_INPUT);
    if (ok != pdPASS) {
        LOG_ERROR("INPUT", "xTaskCreatePinnedToCore(input) failed");
    }
    s_initDone = true;
}

} // namespace InputButtons
