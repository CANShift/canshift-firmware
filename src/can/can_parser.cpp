// can_parser.cpp — CAN frame parser implementation

#include "can_parser.h"
#include "signal_map.h"
#include "runtime/signal_store.h"
#include "config/config_loader.h"
#include "diag/logger.h"

#include <algorithm>

// ---------------------------------------------------------------------------
// Internal state — runtime signal dispatch table
// ---------------------------------------------------------------------------

namespace {

// One entry per decoded signal — built from signals.json at runtime
struct RuntimeSignal {
    uint32_t canFrameId;
    uint8_t startByte;
    uint8_t byteLength; // 1, 2, or 4
    bool bigEndian;
    bool isSigned;
    float scale;
    float offset;
    uint8_t bitMask; // 0 = full value; non-zero = boolean flag extracted from mask
    SignalId signalId;
};

static RuntimeSignal s_runtime[CONFIG_MAX_SIGNALS];
static uint8_t s_runtimeCount = 0;
static bool s_runtimeLoaded = false;

} // namespace

// ---------------------------------------------------------------------------
// Generic multi-byte decoder — lives in CanParser::detail so unit tests
// can link against it. Production callers reach it through `parseFrame`.
// ---------------------------------------------------------------------------

float CanParser::detail::decodeBytes(const uint8_t *data, uint8_t startByte, uint8_t byteLen,
                                     bool bigEndian, bool isSigned, uint8_t bitMask, float scale,
                                     float offset) {
    static constexpr uint16_t kCanFrameMaxBytes = 8;
    if (byteLen == 0 ||
        static_cast<uint16_t>(startByte) + static_cast<uint16_t>(byteLen) > kCanFrameMaxBytes)
        return 0.0f;

    uint32_t raw = 0;
    if (bigEndian) {
        for (uint8_t i = 0; i < byteLen; ++i)
            raw = (raw << 8) | data[startByte + i];
    } else {
        for (uint8_t i = 0; i < byteLen; ++i)
            raw |= static_cast<uint32_t>(data[startByte + i]) << (i * 8);
    }

    // Boolean flag: apply bitmask and return 0 or 1
    if (bitMask != 0)
        return (raw & bitMask) ? 1.0f : 0.0f;

    float physical;
    if (isSigned) {
        const uint8_t bits = static_cast<uint8_t>(byteLen * 8);
        // 64-bit math avoids UB when bits == 32 (shift width >= operand width).
        // For byteLen == 4 the mask is 0 — raw already holds the correct
        // two's-complement bit pattern; the int32_t cast does the reinterpret.
        if (byteLen < 4 && (raw & (1u << (bits - 1))))
            raw |= static_cast<uint32_t>(~((1ULL << bits) - 1ULL));
        physical = static_cast<float>(static_cast<int32_t>(raw));
    } else {
        physical = static_cast<float>(raw);
    }
    return physical * scale + offset;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CanParser::parseFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (!s_runtimeLoaded)
        return;

    // Data-driven dispatch — processes all signals defined for this frame ID.
    // Unknown frame IDs and partial-length frames (start+len > DLC) are
    // silently dropped. No fallback to hardcoded handlers — decoding random
    // frame IDs with assumed semantics produced more wrong data than the
    // value of having any default at all (#682).
    //
    // s_runtime[] is sorted by canFrameId in loadSignalDefinitions(), so we
    // binary-search the first matching entry then forward-scan contiguous
    // matches. Avoids the O(N) walk per received frame (#885).
    const auto *begin = s_runtime;
    const auto *end = s_runtime + s_runtimeCount;
    const auto *it = std::lower_bound(
        begin, end, frameId, [](const RuntimeSignal &s, uint32_t id) { return s.canFrameId < id; });

    for (; it != end && it->canFrameId == frameId; ++it) {
        const RuntimeSignal &sig = *it;
        if (static_cast<uint16_t>(sig.startByte) + static_cast<uint16_t>(sig.byteLength) > length)
            continue;
        const float val = detail::decodeBytes(data, sig.startByte, sig.byteLength, sig.bigEndian,
                                              sig.isSigned, sig.bitMask, sig.scale, sig.offset);
        SignalStore::update(sig.signalId, val);
    }
}

void CanParser::loadSignalDefinitions() {
    const CfgSignalConfig &cfg = ConfigLoader::getSignalConfig();
    if (!cfg.loaded || cfg.signalCount == 0) {
        LOG_ERROR("CAN", "Signal config not loaded — incoming frames will NOT be decoded");
        return;
    }

    s_runtimeCount = 0;
    s_runtimeLoaded = false;

    for (uint8_t i = 0; i < cfg.signalCount && s_runtimeCount < CONFIG_MAX_SIGNALS; ++i) {
        const CfgSignalDef &def = cfg.signals[i];

        // Resolve signal name string → firmware SignalId via the shared
        // single-source-of-truth table in signal_map.cpp.
        const SignalId sid = signalIdFromName(def.name);
        if (sid == SignalIds::SIGNAL_COUNT) {
            LOG_WARN("CAN", "Unknown signal name '%s' in signals.json — skipping", def.name);
            continue;
        }

        RuntimeSignal &r = s_runtime[s_runtimeCount++];
        r.canFrameId = def.canFrameId;
        r.startByte = def.startByte;
        r.byteLength = def.byteLength;
        r.bigEndian = def.bigEndian;
        r.isSigned = def.isSigned;
        r.scale = def.scale;
        r.offset = def.offset;
        r.bitMask = def.bitMask;
        r.signalId = sid;

        // Apply per-signal timeout from the config
        SignalStore::setTimeout(sid, def.timeoutMs);
    }

    // Sort by canFrameId so parseFrame() can binary-search the dispatch table
    // and forward-scan contiguous matches for multi-signal frames (#885).
    std::sort(
        s_runtime, s_runtime + s_runtimeCount,
        [](const RuntimeSignal &a, const RuntimeSignal &b) { return a.canFrameId < b.canFrameId; });

    s_runtimeLoaded = true;
    LOG_INFO("CAN", "Dynamic signal table loaded: %d signals from signals.json", s_runtimeCount);
}
