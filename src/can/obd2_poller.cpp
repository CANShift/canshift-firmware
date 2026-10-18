#include "obd2_poller.h"

#include "can/can_manager.h"
#include "can/can_parser.h"
#include "can/obd2_response.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "config/config_types.h"
#include "runtime/signal_store.h"
#include "diag/logger.h"
#include "app_config.h"

#include <Arduino.h>
#include <string.h>

namespace {

struct PollSlot {
    SignalId signalId;
    uint8_t mode;
    uint8_t pid;
    uint32_t intervalMs;
    uint32_t nextPollMs;
    uint8_t startByte;
    uint8_t byteLength;
    bool bigEndian;
    bool isSigned;
    uint8_t bitMask;
    float scale;
    float offset;
};

PollSlot s_slots[kObd2MaxPollSlots];
uint8_t s_slotCount = 0;
bool s_warnedSlotOverflow = false;

uint32_t s_nextDueMs = 0;

constexpr uint8_t kRequestLen = 0x02;
constexpr uint8_t kRequestDlc = 8;

bool timeReached(uint32_t nowMs, uint32_t dueMs) {
    return static_cast<int32_t>(nowMs - dueMs) >= 0;
}

bool buildRequestFrame(uint8_t mode, uint8_t pid, uint8_t out[kRequestDlc]) {
    if (out == nullptr)
        return false;
    memset(out, 0x00, kRequestDlc);
    out[0] = kRequestLen;
    out[1] = mode;
    out[2] = pid;
    return true;
}

constexpr uint32_t kPollStaggerMs = 25U;

bool hasRoomForSlot() {
    if (s_slotCount < kObd2MaxPollSlots)
        return true;
    if (!s_warnedSlotOverflow) {
        LOG_WARN("OBD2", "Polling slot table full (cap=%u) — dropping extras",
                 static_cast<unsigned>(kObd2MaxPollSlots));
        s_warnedSlotOverflow = true;
    }
    return false;
}

void fillPollSlot(PollSlot &slot, const CfgSignalDef &def, SignalId sid) {
    slot.signalId = sid;
    slot.mode = def.pollMode;
    slot.pid = def.pollPid;
    slot.intervalMs = def.pollIntervalMs;
    slot.nextPollMs = millis() + (static_cast<uint32_t>(s_slotCount) * kPollStaggerMs);
    slot.startByte = def.startByte;
    slot.byteLength = def.byteLength;
    slot.bigEndian = def.bigEndian;
    slot.isSigned = def.isSigned;
    slot.bitMask = def.bitMask;
    slot.scale = def.scale;
    slot.offset = def.offset;
}

void addPollSlot(const CfgSignalDef &def) {
    if (def.pollIntervalMs == 0 || !hasRoomForSlot())
        return;
    const SignalId sid = signalIdFromName(def.name);
    if (sid == SignalIds::kSignalCount) {
        LOG_WARN("OBD2", "Polling signal '%s' has no SignalId mapping — skipped", def.name);
        return;
    }
    PollSlot &slot = s_slots[s_slotCount++];
    fillPollSlot(slot, def, sid);
}

void pollIfDue(PollSlot &slot, uint32_t nowMs) {
    if (!timeReached(nowMs, slot.nextPollMs))
        return;
    uint8_t payload[kRequestDlc];
    if (buildRequestFrame(slot.mode, slot.pid, payload)) {
        const bool sent = CanManager::sendFrame(kObd2RequestFrameId, payload, kRequestDlc, false);
        if (sent)
            LOG_DEBUG("OBD2", "Poll sent mode=0x%02X pid=0x%02X", slot.mode, slot.pid);
        else
            LOG_DEBUG("OBD2", "Poll send failed (pid=0x%02X) — retry next tick", slot.pid);
    }
    slot.nextPollMs = nowMs + slot.intervalMs;
}

uint32_t earliestNextPoll() {
    uint32_t nextDue = s_slots[0].nextPollMs;
    for (uint8_t i = 1; i < s_slotCount; ++i) {
        if (static_cast<int32_t>(s_slots[i].nextPollMs - nextDue) < 0)
            nextDue = s_slots[i].nextPollMs;
    }
    return nextDue;
}

}

void Obd2Poller::init() {
    s_slotCount = 0;
    s_warnedSlotOverflow = false;
    s_nextDueMs = 0;

    const CfgSignalConfig &cfg = ConfigLoader::getSignalConfig();
    if (!cfg.loaded || cfg.signalCount == 0) {
        LOG_INFO("OBD2", "No signal config loaded — polling disabled");
        return;
    }

    for (uint8_t i = 0; i < cfg.signalCount; ++i)
        addPollSlot(cfg.signals[i]);

    LOG_INFO("OBD2", "Polling enabled for %u signal(s)", static_cast<unsigned>(s_slotCount));
}

void Obd2Poller::tick(uint32_t nowMs) {
    if (s_slotCount == 0)
        return;

    if (!timeReached(nowMs, s_nextDueMs))
        return;

    for (uint8_t i = 0; i < s_slotCount; ++i)
        pollIfDue(s_slots[i], nowMs);
    s_nextDueMs = earliestNextPoll();
}

bool Obd2Poller::onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (s_slotCount == 0)
        return false;
    if (frameId != kObd2ResponseFrameId)
        return false;

    const Obd2Response::Header header = Obd2Response::parseHeader(data, length);
    if (!header.ok)
        return false;

    for (uint8_t i = 0; i < s_slotCount; ++i) {
        PollSlot &slot = s_slots[i];
        if (!Obd2Response::matches(header, slot.mode, slot.pid))
            continue;

        if (!Obd2Response::fitsValueBytes(slot.startByte, slot.byteLength, length))
            continue;

        const float physical = CanParser::detail::decodeBytes(
            data, slot.startByte, slot.byteLength, slot.bigEndian, slot.isSigned, slot.bitMask,
            slot.scale, slot.offset, length);
        SignalStore::update(slot.signalId, physical);
        LOG_DEBUG("OBD2", "Response decoded pid=0x%02X value=%.3f", slot.pid,
                  static_cast<double>(physical));
        return true;
    }

    return false;
}
