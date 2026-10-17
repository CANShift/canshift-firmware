#include "alert_takeover_broadcast.h"

#include "diag/error_store.h"
#include "diag/logger.h"
#include "hal/ble/alert_frame.h"
#include "runtime/alert_broadcast.h"
#include "runtime/signal_store.h"
#include "ui/alert_sources.h"

#include <stddef.h>

namespace {

static_assert(AlertSources::kMaxSourceCount == AlertFrame::kSourceCount);

struct Snapshot {
    uint32_t critical;
    uint32_t acknowledged;
    int displayed;
    uint32_t nowMs;
};

AlertFrame::Onsets s_onsets = {};

bool isSet(uint32_t mask, size_t source) {
    return (mask & (1u << source)) != 0;
}

AlertFrame::Entry entryFor(size_t source, const Snapshot &snap) {
    const AlertSources::CriticalSource &src = AlertSources::at(source);
    const float reading = SignalStore::read(src.id);
    const AlertEngine::CriticalLimit limit = AlertSources::limitAt(source, reading);
    const uint8_t wireSource = static_cast<uint8_t>(source);
    return {wireSource,
            reading,
            limit.valid,
            limit.limit,
            limit.below,
            isSet(snap.acknowledged, source),
            static_cast<int>(source) == snap.displayed,
            AlertFrame::activeSeconds(s_onsets, wireSource, snap.nowMs),
            src.signalKey};
}

size_t collectEntries(AlertFrame::Entry *out, const Snapshot &snap) {
    size_t count = 0;
    if (snap.displayed >= 0)
        out[count++] = entryFor(static_cast<size_t>(snap.displayed), snap);
    for (size_t source = 0; source < AlertSources::count(); ++source) {
        if (!isSet(snap.critical, source) || static_cast<int>(source) == snap.displayed)
            continue;
        out[count++] = entryFor(source, snap);
    }
    return count;
}

} // namespace

void AlertTakeoverBroadcast::publish(uint32_t critical, uint32_t acknowledged, int displayed,
                                     uint32_t nowMs) {
    AlertFrame::trackOnsets(s_onsets, critical, nowMs);
    const Snapshot snap = {critical, acknowledged, displayed, nowMs};
    AlertFrame::Entry entries[AlertFrame::kSourceCount];
    const size_t count = collectEntries(entries, snap);
    uint8_t frame[AlertFrame::kMaxFrameBytes];
    const size_t len = AlertFrame::encode(entries, count, frame, sizeof(frame));
    if (len == 0) {
        LOG_ERROR("ALERT", "alert frame encode failed — BLE alert state not refreshed");
        ErrorStore::push(ERROR_SRC_SYSTEM, "ALERT_FRAME", "alert frame encode failed");
        return;
    }
    AlertBroadcast::publish(frame, len);
}
