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
    bool hasExpr;
    uint8_t tokenCount;
    FfiTok tokens[CANSHIFT_EXPR_MAX_TOKENS];
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
            !sig.hasExpr
                ? decode_bytes_rs(data, sig.startByte, sig.byteLength, sig.bigEndian, sig.isSigned,
                                  sig.bitMask, sig.scale, sig.offset)
                : eval_tokens_rs(data, sig.startByte, sig.byteLength, sig.bigEndian, sig.isSigned,
                                 sig.bitMask, sig.scale, sig.offset, sig.tokens, sig.tokenCount);
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
        r.hasExpr = exprLen > 0;
        r.tokenCount = 0;
        if (r.hasExpr) {
            const int32_t n = lex_expr_rs(reinterpret_cast<const uint8_t *>(def.expr), exprLen,
                                          r.tokens, CANSHIFT_EXPR_MAX_TOKENS);
            if (n > 0)
                r.tokenCount = static_cast<uint8_t>(n);
        }

        SignalStore::setTimeout(sid, def.timeoutMs);
    }

    std::sort(
        s_runtime, s_runtime + s_runtimeCount,
        [](const RuntimeSignal &a, const RuntimeSignal &b) { return a.canFrameId < b.canFrameId; });

    s_runtimeLoaded = true;
    LOG_INFO("CAN", "Dynamic signal table loaded: %d signals from signals.json", s_runtimeCount);
}
