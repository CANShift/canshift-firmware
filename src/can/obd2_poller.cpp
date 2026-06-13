#include "obd2_poller.h"

#include "can/can_manager.h"
#include "can/can_parser.h"
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
    uint32_t lastResponseMs;
    uint8_t startByte;
    uint8_t byteLength;
    bool bigEndian;
    bool isSigned;
    uint8_t bitMask;
    float scale;
    float offset;
};

PollSlot s_slots[OBD2_MAX_POLL_SLOTS];
uint8_t s_slotCount = 0;
uint32_t s_pollsSent = 0;
uint32_t s_responsesMatched = 0;
uint32_t s_responsesMissed = 0;
bool s_warnedSlotOverflow = false;

uint32_t s_nextDueMs = 0;

constexpr uint8_t kRequestLen = 0x02;
constexpr uint8_t kRequestDlc = 8;

constexpr uint8_t kResponseModeMask = 0x40;

bool buildRequestFrame(uint8_t mode, uint8_t pid, uint8_t out[kRequestDlc]) {
    if (out == nullptr)
        return false;
    memset(out, 0x00, kRequestDlc);
    out[0] = kRequestLen;
    out[1] = mode;
    out[2] = pid;
    return true;
}

} // namespace

void Obd2Poller::init() {
    s_slotCount = 0;
    s_pollsSent = 0;
    s_responsesMatched = 0;
    s_responsesMissed = 0;
    s_warnedSlotOverflow = false;
    s_nextDueMs = 0;

    const CfgSignalConfig &cfg = ConfigLoader::getSignalConfig();
    if (!cfg.loaded || cfg.signalCount == 0) {
        LOG_INFO("OBD2", "No signal config loaded — polling disabled");
        return;
    }

    for (uint8_t i = 0; i < cfg.signalCount; ++i) {
        const CfgSignalDef &def = cfg.signals[i];
        if (def.pollIntervalMs == 0)
            continue;

        if (s_slotCount >= OBD2_MAX_POLL_SLOTS) {
            if (!s_warnedSlotOverflow) {
                LOG_WARN("OBD2", "Polling slot table full (cap=%u) — dropping extras",
                         static_cast<unsigned>(OBD2_MAX_POLL_SLOTS));
                s_warnedSlotOverflow = true;
            }
            continue;
        }

        const SignalId sid = signalIdFromName(def.name);
        if (sid == SignalIds::SIGNAL_COUNT) {
            LOG_WARN("OBD2", "Polling signal '%s' has no SignalId mapping — skipped", def.name);
            continue;
        }

        PollSlot &slot = s_slots[s_slotCount++];
        slot.signalId = sid;
        slot.mode = def.pollMode;
        slot.pid = def.pollPid;
        slot.intervalMs = def.pollIntervalMs;

        slot.nextPollMs = millis() + (static_cast<uint32_t>(s_slotCount) * 25U);
        slot.lastResponseMs = 0;
        slot.startByte = def.startByte;
        slot.byteLength = def.byteLength;
        slot.bigEndian = def.bigEndian;
        slot.isSigned = def.isSigned;
        slot.bitMask = def.bitMask;
        slot.scale = def.scale;
        slot.offset = def.offset;
    }

    LOG_INFO("OBD2", "Polling enabled for %u signal(s)", static_cast<unsigned>(s_slotCount));
}

void Obd2Poller::tick(uint32_t nowMs) {
    if (s_slotCount == 0)
        return;

    if (nowMs < s_nextDueMs)
        return;

    for (uint8_t i = 0; i < s_slotCount; ++i) {
        PollSlot &slot = s_slots[i];
        if (nowMs < slot.nextPollMs)
            continue;

        if (slot.lastResponseMs == 0 || slot.lastResponseMs < slot.nextPollMs - slot.intervalMs) {

            if (s_pollsSent > 0)
                ++s_responsesMissed;
        }

        uint8_t payload[kRequestDlc];
        if (!buildRequestFrame(slot.mode, slot.pid, payload)) {

            slot.nextPollMs = nowMs + slot.intervalMs;
            continue;
        }

        const bool sent = CanManager::sendFrame(OBD2_REQUEST_FRAME_ID, payload, kRequestDlc, false);
        if (sent) {
            ++s_pollsSent;
            LOG_DEBUG("OBD2", "Poll sent mode=0x%02X pid=0x%02X", slot.mode, slot.pid);
        } else {

            LOG_DEBUG("OBD2", "Poll send failed (pid=0x%02X) — retry next tick", slot.pid);
        }
        slot.nextPollMs = nowMs + slot.intervalMs;
    }

    uint32_t nextDue = UINT32_MAX;
    for (uint8_t i = 0; i < s_slotCount; ++i) {
        if (s_slots[i].nextPollMs < nextDue)
            nextDue = s_slots[i].nextPollMs;
    }
    s_nextDueMs = nextDue;
}

bool Obd2Poller::onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (s_slotCount == 0)
        return false;
    if (frameId != OBD2_RESPONSE_FRAME_ID)
        return false;
    if (data == nullptr || length < 3)
        return false;

    const uint8_t payloadLen = data[0];
    const uint8_t modeEcho = data[1];
    const uint8_t pidEcho = data[2];
    if (payloadLen < 2 || payloadLen > 6)
        return false;

    for (uint8_t i = 0; i < s_slotCount; ++i) {
        PollSlot &slot = s_slots[i];
        const uint8_t expectedModeEcho = static_cast<uint8_t>(slot.mode | kResponseModeMask);
        if (modeEcho != expectedModeEcho || pidEcho != slot.pid)
            continue;

        const float physical =
            CanParser::detail::decodeBytes(data, slot.startByte, slot.byteLength, slot.bigEndian,
                                           slot.isSigned, slot.bitMask, slot.scale, slot.offset);
        SignalStore::update(slot.signalId, physical);
        slot.lastResponseMs = millis();
        ++s_responsesMatched;
        LOG_DEBUG("OBD2", "Response decoded pid=0x%02X value=%.3f", slot.pid,
                  static_cast<double>(physical));
        return true;
    }

    return false;
}

uint32_t Obd2Poller::pollsSent() {
    return s_pollsSent;
}

uint32_t Obd2Poller::responsesMatched() {
    return s_responsesMatched;
}

uint32_t Obd2Poller::responsesMissed() {
    return s_responsesMissed;
}

uint8_t Obd2Poller::activeSlotCount() {
    return s_slotCount;
}
