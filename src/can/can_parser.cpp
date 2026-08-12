#include "can_parser.h"
#include "app_config.h"
#include "signal_map.h"
#include "runtime/signal_store.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "can_parser_rs.h"

#include <algorithm>
#include <cmath>
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
    uint8_t refCount;
    uint16_t refTargetIds[CFG_MAX_EXPR_REFS];
    FfiTok tokens[CANSHIFT_EXPR_MAX_TOKENS];
};

struct TargetBinding {
    uint16_t targetId;
    SignalId signalId;
};

static RuntimeSignal s_runtime[CONFIG_MAX_SIGNALS];
static uint8_t s_runtimeCount = 0;
static bool s_runtimeLoaded = false;
static TargetBinding s_targets[CONFIG_MAX_SIGNALS];
static uint8_t s_targetCount = 0;

// A cross-signal expression reads values its own frame does not carry, so it
// runs after every plain signal in the frame has been stored.
bool resolveRefs(const RuntimeSignal &sig, RefValueRs *out) {
    for (uint8_t i = 0; i < sig.refCount; ++i) {
        SignalId sid = SignalIds::SIGNAL_COUNT;
        for (uint8_t t = 0; t < s_targetCount; ++t) {
            if (s_targets[t].targetId == sig.refTargetIds[i]) {
                sid = s_targets[t].signalId;
                break;
            }
        }
        if (sid >= SIGNAL_STORE_MAX_SIGNALS || !SignalStore::isValid(sid))
            return false;
        out[i].target_id = sig.refTargetIds[i];
        out[i].value = SignalStore::read(sid);
    }
    return true;
}

void evalCrossSignal(const RuntimeSignal &sig, const uint8_t *data, uint8_t length) {
    RefValueRs refs[CFG_MAX_EXPR_REFS] = {};
    if (!resolveRefs(sig, refs))
        return;
    const float val = eval_tokens_refs_rs(data, sig.startByte, sig.byteLength, sig.bigEndian,
                                          sig.isSigned, sig.bitMask, sig.scale, sig.offset, length,
                                          sig.tokens, sig.tokenCount, refs, sig.refCount);
    if (isnan(val))
        return;
    SignalStore::update(sig.signalId, val);
}

} // namespace

float CanParser::detail::decodeBytes(const uint8_t *data, uint8_t startByte, uint8_t byteLen,
                                     bool bigEndian, bool isSigned, uint8_t bitMask, float scale,
                                     float offset, size_t dataLen) {
    return decode_bytes_rs(data, startByte, byteLen, bigEndian, isSigned, bitMask, scale, offset,
                           dataLen);
}

void CanParser::parseFrame(uint32_t frameId, const uint8_t *data, uint8_t length) {
    if (!s_runtimeLoaded)
        return;

    const auto *begin = s_runtime;
    const auto *end = s_runtime + s_runtimeCount;
    const auto *it = std::lower_bound(
        begin, end, frameId, [](const RuntimeSignal &s, uint32_t id) { return s.canFrameId < id; });

    for (const auto *cur = it; cur != end && cur->canFrameId == frameId; ++cur) {
        const RuntimeSignal &sig = *cur;
        if (sig.refCount > 0)
            continue;
        if (static_cast<uint16_t>(sig.startByte) + static_cast<uint16_t>(sig.byteLength) > length)
            continue;
        const float val =
            !sig.hasExpr ? decode_bytes_rs(data, sig.startByte, sig.byteLength, sig.bigEndian,
                                           sig.isSigned, sig.bitMask, sig.scale, sig.offset, length)
                         : eval_tokens_rs(data, sig.startByte, sig.byteLength, sig.bigEndian,
                                          sig.isSigned, sig.bitMask, sig.scale, sig.offset, length,
                                          sig.tokens, sig.tokenCount);
        SignalStore::update(sig.signalId, val);
    }

    for (; it != end && it->canFrameId == frameId; ++it) {
        if (it->refCount == 0)
            continue;
        if (static_cast<uint16_t>(it->startByte) + static_cast<uint16_t>(it->byteLength) > length)
            continue;
        evalCrossSignal(*it, data, length);
    }
}

void CanParser::loadSignalDefinitions() {
    const CfgSignalConfig &cfg = ConfigLoader::getSignalConfig();
    if (!cfg.loaded || cfg.signalCount == 0) {
        LOG_ERROR("CAN", "Signal config not loaded — incoming frames will NOT be decoded");
        return;
    }

    s_runtimeCount = 0;
    s_targetCount = 0;
    s_runtimeLoaded = false;

    for (uint8_t i = 0; i < cfg.signalCount && s_runtimeCount < CONFIG_MAX_SIGNALS; ++i) {
        const CfgSignalDef &def = cfg.signals[i];

        const SignalId sid = signalIdFromName(def.name);
        if (sid == SignalIds::SIGNAL_COUNT) {
            LOG_WARN("CAN", "Unknown signal name '%s' in signals.json — skipping", def.name);
            continue;
        }

        if (def.hasTargetId && s_targetCount < CONFIG_MAX_SIGNALS) {
            s_targets[s_targetCount].targetId = def.targetId;
            s_targets[s_targetCount].signalId = sid;
            ++s_targetCount;
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
        r.refCount = def.exprRefCount;
        for (uint8_t i = 0; i < def.exprRefCount; ++i)
            r.refTargetIds[i] = def.exprRefs[i];
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
