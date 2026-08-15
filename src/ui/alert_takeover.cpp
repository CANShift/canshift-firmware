#include "alert_takeover.h"
#include "can/signal_map.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"
#include "ui/alert_sources.h"
#include "ui/alert_takeover_view.h"
#include "ui/ota_overlay.h"
#include "util/format_float.h"
#include "util/text_join.h"

#include <Arduino.h>
#include <atomic>
#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t kUpdatePeriodMs = 100;
constexpr int kNoActiveSource = -1;
constexpr const char *kLimitWord[] = {"MAX", "MIN"};

uint32_t s_lastUpdateMs = 0;
int s_activeSource = kNoActiveSource;
int s_acknowledgedSource = kNoActiveSource;
std::atomic<bool> s_activeFlag{false};
std::atomic<bool> s_ackRequested{false};
char s_lastValueText[16] = "";
char s_lastContextText[56] = "";

void ackClickCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    s_ackRequested.store(true, std::memory_order_relaxed);
}

int pickCriticalSource(const AlertEngine::AlertState &state) {
    for (size_t i = 0; i < AlertSources::kSourceCount; ++i) {
        const AlertSources::CriticalSource &src = AlertSources::kSources[i];
        if (state.*(src.level) != AlertEngine::AlertLevel::CRITICAL)
            continue;
        if (state.*(src.sensorLost) && !SignalStore::isValid(src.id))
            continue;
        return static_cast<int>(i);
    }
    return kNoActiveSource;
}

void showFor(int sourceIndex) {
    s_lastValueText[0] = '\0';
    s_lastContextText[0] = '\0';
    AlertTakeoverView::setSignalName(AlertSources::kSources[sourceIndex].takeoverName);
    AlertTakeoverView::setHidden(false);
}

void hideLayer() {
    if (s_activeSource == kNoActiveSource)
        return;
    s_activeSource = kNoActiveSource;
    s_activeFlag.store(false, std::memory_order_relaxed);
    AlertTakeoverView::setHidden(true);
}

void refreshValue(const AlertSources::CriticalSource &src) {
    char text[16];
    FloatFormat::formatFixed(text, sizeof(text), SignalStore::read(src.id), src.decimals);
    if (strcmp(text, s_lastValueText) == 0)
        return;
    strlcpy(s_lastValueText, text, sizeof(s_lastValueText));
    AlertTakeoverView::setValue(text);
}

void formatContext(char *out, size_t outLen, const AlertSources::CriticalSource &src) {
    const int rpm = static_cast<int>(lroundf(SignalStore::read(SignalIds::RPM)));
    const AlertEngine::CriticalLimit limit =
        AlertEngine::criticalLimitFor(src.id, SignalStore::read(src.id));
    if (!limit.valid) {
        snprintf(out, outLen, "AT %d rpm", rpm);
        return;
    }
    char limitText[12];
    FloatFormat::formatFixed(limitText, sizeof(limitText), limit.limit, src.decimals);
    snprintf(out, outLen, "%s %s %s%sAT %d rpm", kLimitWord[limit.below ? 1 : 0], limitText,
             src.unit, TextJoin::kSeparator, rpm);
}

void refreshContext(const AlertSources::CriticalSource &src) {
    char text[56];
    formatContext(text, sizeof(text), src);
    if (strcmp(text, s_lastContextText) == 0)
        return;
    strlcpy(s_lastContextText, text, sizeof(s_lastContextText));
    AlertTakeoverView::setContext(text);
}

} // namespace

void AlertTakeover::init() {
    AlertTakeoverView::build(ackClickCb);
    s_lastUpdateMs = 0;
    s_activeSource = kNoActiveSource;
    s_acknowledgedSource = kNoActiveSource;
}

void AlertTakeover::update() {
    if (!AlertTakeoverView::isBuilt())
        return;

    const uint32_t now = millis();
    if (now - s_lastUpdateMs < kUpdatePeriodMs)
        return;
    s_lastUpdateMs = now;

    if (s_ackRequested.exchange(false, std::memory_order_relaxed) &&
        s_activeSource != kNoActiveSource) {
        s_acknowledgedSource = s_activeSource;
    }

    const AlertEngine::AlertState state = AlertEngine::getState();
    const int critical = OtaOverlay::isActive() ? kNoActiveSource : pickCriticalSource(state);

    if (critical == kNoActiveSource) {
        s_acknowledgedSource = kNoActiveSource;
        hideLayer();
        return;
    }

    if (critical == s_acknowledgedSource) {
        hideLayer();
        return;
    }

    if (critical != s_activeSource) {
        s_activeSource = critical;
        s_activeFlag.store(true, std::memory_order_relaxed);
        showFor(critical);
    }
    refreshValue(AlertSources::kSources[critical]);
    refreshContext(AlertSources::kSources[critical]);
}

bool AlertTakeover::isActive() {
    return s_activeFlag.load(std::memory_order_relaxed);
}

void AlertTakeover::requestAcknowledge() {
    s_ackRequested.store(true, std::memory_order_relaxed);
}
