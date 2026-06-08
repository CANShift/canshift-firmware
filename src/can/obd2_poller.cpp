// obd2_poller.cpp — OBD-II request/response poller (issue #841)
//
// One static `PollSlot` per signal that carries a `polling` block in
// signals.json. The scheduler fires a request frame at `intervalMs` and the
// RX hook decodes the matching response into SignalStore via the same
// generic byte decoder CanParser uses for broadcast frames.

#include "obd2_poller.h"

#include "can/can_manager.h"
#include "can/can_parser.h"
#include "can/signal_map.h"
#include "config/config_loader.h"
#include "config/config_types.h"
#include "runtime/signal_store.h"
#include "diag/logger.h"
#include "app_config.h"

#include <Arduino.h> // millis()
#include <string.h>

namespace {

// One polling slot — populated from `CfgSignalDef` entries that carry a
// non-zero `pollIntervalMs`. The decoder fields (startByte/byteLength/etc.)
// are copied here so the RX hook does not have to chase `ConfigLoader`
// during the hot path.
struct PollSlot {
    SignalId signalId;
    uint8_t mode; // OBD-II mode byte (0x01 for v1)
    uint8_t pid;
    uint32_t intervalMs;
    uint32_t nextPollMs;
    uint32_t lastResponseMs;
    // Decode fields — mirrors CfgSignalDef so OnRxFrame doesn't reach back
    // into ConfigLoader for every response.
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

// Earliest `nextPollMs` across all active slots. Lets `tick()` short-circuit
// without walking the whole table on every CAN-loop iteration (#1343). Reset
// to 0 in `init()` so the first tick always enters the slow path and rebuilds
// the cache.
uint32_t s_nextDueMs = 0;

// OBD-II Mode 01 request payload layout (single PID):
//   [len=0x02] [mode=0x01] [pid] [0x00 x 5 padding]
// The padding bytes are arbitrary on a strictly-spec-compliant ECU but most
// tooling and adapters pad with 0x00, so we match that for least surprise.
constexpr uint8_t kRequestLen = 0x02;
constexpr uint8_t kRequestDlc = 8;

// Response decoder layout — A lives at byte 3 (after length / mode echo / PID
// echo). See SAE J1979 frame structure documented in obd2_poller.h.
constexpr uint8_t kResponseModeMask = 0x40; // request mode | 0x40 == response mode

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

// ---------------------------------------------------------------------------
// Init — scan the loaded signal config and copy polling-mode signals into
// the slot table. Idempotent: a re-call after a config reload simply rebuilds
// the table from scratch.
// ---------------------------------------------------------------------------

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
            continue; // Broadcast-mode signal — handled by CanParser.

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
        // Stagger first polls so a config with many signals does not slam
        // the bus on boot. 25 ms per slot keeps even a 32-signal worst case
        // inside a 1 s startup window without bunching requests.
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

// ---------------------------------------------------------------------------
// Scheduler tick — send each due request, advance its nextPollMs. Misses
// (no response since the previous tick) are counted but do NOT delay the
// next attempt; OBD-II ECUs occasionally drop a frame and retrying is the
// expected recovery.
// ---------------------------------------------------------------------------

void Obd2Poller::tick(uint32_t nowMs) {
    if (s_slotCount == 0)
        return;

    // Fast path: nothing is due yet. taskCAN yields only every
    // CAN_BURST_BEFORE_YIELD frames so on a busy bus we land here hundreds of
    // times per second; without the cache the inner loop walked all 16 slots
    // each call just to confirm none were due (#1343).
    if (nowMs < s_nextDueMs)
        return;

    for (uint8_t i = 0; i < s_slotCount; ++i) {
        PollSlot &slot = s_slots[i];
        if (nowMs < slot.nextPollMs)
            continue;

        // Account for a miss BEFORE re-sending so the metric reflects "we
        // didn't hear back before the next scheduled poll".
        if (slot.lastResponseMs == 0 || slot.lastResponseMs < slot.nextPollMs - slot.intervalMs) {
            // Only count misses after the very first send (lastResponseMs==0 on
            // the first send before any response arrived is normal — skip then).
            if (s_pollsSent > 0)
                ++s_responsesMissed;
        }

        uint8_t payload[kRequestDlc];
        if (!buildRequestFrame(slot.mode, slot.pid, payload)) {
            // Defensive — buildRequestFrame only fails on nullptr out.
            slot.nextPollMs = nowMs + slot.intervalMs;
            continue;
        }

        const bool sent = CanManager::sendFrame(OBD2_REQUEST_FRAME_ID, payload, kRequestDlc, false);
        if (sent) {
            ++s_pollsSent;
            LOG_DEBUG("OBD2", "Poll sent mode=0x%02X pid=0x%02X", slot.mode, slot.pid);
        } else {
            // TX queue full or driver error — retry on the next tick rather
            // than skipping the slot entirely.
            LOG_DEBUG("OBD2", "Poll send failed (pid=0x%02X) — retry next tick", slot.pid);
        }
        slot.nextPollMs = nowMs + slot.intervalMs;
    }

    // Refresh the gate from the freshly-updated slot table. Cheap min-walk
    // (no per-slot work) and only runs when we actually entered the loop.
    uint32_t nextDue = UINT32_MAX;
    for (uint8_t i = 0; i < s_slotCount; ++i) {
        if (s_slots[i].nextPollMs < nextDue)
            nextDue = s_slots[i].nextPollMs;
    }
    s_nextDueMs = nextDue;
}

// ---------------------------------------------------------------------------
// RX hook — match an inbound frame to a pending PID, decode A..D, push to
// SignalStore. Returns true to short-circuit the broadcast parser so a
// passive-broadcast entry on the same response ID does not double-decode.
// ---------------------------------------------------------------------------

bool Obd2Poller::onRxFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (s_slotCount == 0)
        return false;
    if (frameId != OBD2_RESPONSE_FRAME_ID)
        return false;
    if (data == nullptr || length < 3)
        return false;

    // Validate the response envelope. byte 0 = length, byte 1 = mode echo
    // (request mode | 0x40), byte 2 = PID echo. Anything malformed and we
    // bail to false so the broadcast parser still sees it (might be a real
    // 0x7E8 broadcast on a non-OBD2 ECU sharing the ID).
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

        // Decode through the same generic helper that broadcast frames use —
        // avoids duplicating the endianness / sign-extend / bitmask logic
        // and keeps every per-byte fix in one place.
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

    // 0x7E8 frame but no matching PID slot — let the broadcast parser try it.
    return false;
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

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
