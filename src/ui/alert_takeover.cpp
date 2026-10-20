#include "alert_takeover.h"
#include "alert_engine_rs.h"
#include "can/signal_map.h"
#include "runtime/alert_engine.h"
#include "runtime/signal_store.h"
#include "ui/alert_sources.h"
#include "ui/alert_takeover_broadcast.h"
#include "ui/alert_takeover_view.h"
#include "ui/ota_overlay.h"
#include "ui/unit_display.h"
#include "util/text_join.h"

#include <Arduino.h>
#include <atomic>
#include <cmath>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t kUpdatePeriodMs = 100;
constexpr int kNoActiveSource = kAlertCriticalNoSource;
constexpr const char *kLimitWord[] = {"MAX", "MIN"};

uint32_t s_lastUpdateMs = 0;
int s_activeSource = kNoActiveSource;
uint32_t s_criticalSources = 0;
uint32_t s_acknowledgedSources = 0;
std::atomic<bool> s_activeFlag{false};
std::atomic<bool> s_ackRequested{false};
std::atomic<uint32_t> s_ackRequestedSources{0};
char s_lastValueText[16] = "";
char s_lastContextText[56] = "";

void ackClickCb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    s_ackRequested.store(true, std::memory_order_relaxed);
}

static_assert(AlertSources::kMaxSourceCount <= kAlertCriticalSourceCapacity);

bool isShowable(const AlertEngine::AlertState &state, size_t index) {
    if (AlertSources::levelAt(state, index) != AlertEngine::AlertLevel::CRITICAL)
        return false;
    return !(AlertSources::sensorLostAt(state, index) &&
             !SignalStore::isValid(AlertSources::at(index).id));
}

uint32_t criticalSources(const AlertEngine::AlertState &state) {
    uint32_t critical = 0;
    for (size_t i = 0; i < AlertSources::count(); ++i) {
        if (isShowable(state, i))
            critical |= 1u << i;
    }
    return critical;
}

int pickCriticalSource(const AlertEngine::AlertState &state) {
    s_criticalSources = OtaOverlay::suspendsDash() ? 0 : criticalSources(state);
    s_acknowledgedSources = alert_critical_retain_rs(s_acknowledgedSources, s_criticalSources);
    return alert_critical_first_unacknowledged_rs(s_criticalSources, s_acknowledgedSources);
}

void consumeAcknowledge() {
    s_acknowledgedSources |= s_ackRequestedSources.exchange(0, std::memory_order_relaxed);
    if (!s_ackRequested.exchange(false, std::memory_order_relaxed))
        return;
    s_acknowledgedSources =
        alert_critical_acknowledge_rs(s_acknowledgedSources, static_cast<int8_t>(s_activeSource));
}

void showFor(int sourceIndex) {
    s_lastValueText[0] = '\0';
    s_lastContextText[0] = '\0';
    AlertTakeoverView::setSignalName(
        AlertSources::at(static_cast<size_t>(sourceIndex)).takeoverName);
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
    AlertSources::formatReading(text, sizeof(text), src, SignalStore::read(src.id));
    if (strcmp(text, s_lastValueText) == 0)
        return;
    strlcpy(s_lastValueText, text, sizeof(s_lastValueText));
    AlertTakeoverView::setValue(text);
}

void formatContext(char *out, size_t outLen, size_t index) {
    const AlertSources::CriticalSource &src = AlertSources::at(index);
    const int rpm = static_cast<int>(lroundf(SignalStore::read(SignalIds::kRpm)));
    const AlertEngine::CriticalLimit limit =
        AlertSources::limitAt(index, SignalStore::read(src.id));
    if (!limit.valid) {
        snprintf(out, outLen, "AT %d rpm", rpm);
        return;
    }
    char limitText[12];
    AlertSources::formatReading(limitText, sizeof(limitText), src, limit.limit);
    snprintf(out, outLen, "%s %s %s%sAT %d rpm", kLimitWord[limit.below ? 1 : 0], limitText,
             UnitDisplay::symbolFor(src.unit), TextJoin::kSeparator, rpm);
}

void refreshContext(size_t index) {
    char text[56];
    formatContext(text, sizeof(text), index);
    if (strcmp(text, s_lastContextText) == 0)
        return;
    strlcpy(s_lastContextText, text, sizeof(s_lastContextText));
    AlertTakeoverView::setContext(text);
}

}

void AlertTakeover::init() {
    AlertTakeoverView::build(ackClickCb);
    s_lastUpdateMs = 0;
    s_activeSource = kNoActiveSource;
    s_criticalSources = 0;
    s_acknowledgedSources = 0;
}

void AlertTakeover::update() {
    if (!AlertTakeoverView::isBuilt())
        return;

    const uint32_t now = millis();
    if (now - s_lastUpdateMs < kUpdatePeriodMs)
        return;
    s_lastUpdateMs = now;

    consumeAcknowledge();
    const int critical = pickCriticalSource(AlertEngine::getState());
    AlertTakeoverBroadcast::publish(s_criticalSources, s_acknowledgedSources, critical, now);
    if (critical == kNoActiveSource) {
        hideLayer();
        return;
    }

    if (critical != s_activeSource) {
        s_activeSource = critical;
        s_activeFlag.store(true, std::memory_order_relaxed);
        showFor(critical);
    }
    refreshValue(AlertSources::at(static_cast<size_t>(critical)));
    refreshContext(static_cast<size_t>(critical));
}

void AlertTakeover::reset() {
    s_ackRequested.store(false, std::memory_order_relaxed);
    s_ackRequestedSources.store(0, std::memory_order_relaxed);
    s_criticalSources = 0;
    s_acknowledgedSources = 0;
    hideLayer();
}

bool AlertTakeover::isActive() {
    return s_activeFlag.load(std::memory_order_relaxed);
}

void AlertTakeover::requestAcknowledge() {
    s_ackRequested.store(true, std::memory_order_relaxed);
}

void AlertTakeover::requestAcknowledgeSource(uint8_t source) {
    if (source >= kAlertCriticalSourceCapacity)
        return;
    s_ackRequestedSources.fetch_or(1u << source, std::memory_order_relaxed);
}
