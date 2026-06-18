#include "can_parser.h"
#include "app_config.h"
#include "signal_map.h"
#include "runtime/signal_store.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "can_parser_rs.h"

#include <algorithm>
#include <string.h>

namespace {

struct RuntimeSignal {
    uint32_t canFrameId;
    uint8_t startByte;
    uint8_t byteLength;
    bool bigEndian;
    bool isSigned;
    float scale;
    float offset;
    uint8_t bitMask;
    SignalId signalId;
    uint8_t exprLen;
    char expr[CFG_MAX_EXPR_LEN];
};

static RuntimeSignal s_runtime[CONFIG_MAX_SIGNALS];
static uint8_t s_runtimeCount = 0;
static bool s_runtimeLoaded = false;

} // namespace

float CanParser::detail::decodeBytes(const uint8_t *data, uint8_t startByte, uint8_t byteLen,
                                     bool bigEndian, bool isSigned, uint8_t bitMask, float scale,
                                     float offset) {
    return decode_bytes_rs(data, startByte, byteLen, bigEndian, isSigned, bitMask, scale, offset);
}

void CanParser::parseFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (!s_runtimeLoaded)
        return;

    const auto *begin = s_runtime;
    const auto *end = s_runtime + s_runtimeCount;
    const auto *it = std::lower_bound(
        begin, end, frameId, [](const RuntimeSignal &s, uint32_t id) { return s.canFrameId < id; });

    for (; it != end && it->canFrameId == frameId; ++it) {
        const RuntimeSignal &sig = *it;
        if (static_cast<uint16_t>(sig.startByte) + static_cast<uint16_t>(sig.byteLength) > length)
            continue;
        const float val =
            sig.exprLen == 0
                ? decode_bytes_rs(data, sig.startByte, sig.byteLength, sig.bigEndian, sig.isSigned,
                                  sig.bitMask, sig.scale, sig.offset)
                : decode_with_expr_rs(data, sig.startByte, sig.byteLength, sig.bigEndian,
                                      sig.isSigned, sig.bitMask, sig.scale, sig.offset,
                                      reinterpret_cast<const uint8_t *>(sig.expr), sig.exprLen);
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
        const size_t exprLen = strnlen(def.expr, sizeof(def.expr));
        r.exprLen = static_cast<uint8_t>(exprLen);
        if (exprLen > 0)
            memcpy(r.expr, def.expr, exprLen);

        SignalStore::setTimeout(sid, def.timeoutMs);
    }

    std::sort(
        s_runtime, s_runtime + s_runtimeCount,
        [](const RuntimeSignal &a, const RuntimeSignal &b) { return a.canFrameId < b.canFrameId; });

    s_runtimeLoaded = true;
    LOG_INFO("CAN", "Dynamic signal table loaded: %d signals from signals.json", s_runtimeCount);
}
