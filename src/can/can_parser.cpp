#include "can_parser.h"
#include "app_config.h"
#include "signal_map.h"
#include "runtime/signal_store.h"
#include "config/config_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "can_parser_rs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
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
    uint16_t tokenOffset;
    uint16_t refTargetIds[kCfgMaxExprRefs];
};

struct TargetBinding {
    uint16_t targetId;
    SignalId signalId;
};

constexpr size_t kExprPoolTokens = 512;

static RuntimeSignal s_runtime[kConfigMaxSignals];
static FfiTok s_exprPool[kExprPoolTokens];
static size_t s_exprPoolUsed = 0;
static uint8_t s_runtimeCount = 0;
static bool s_runtimeLoaded = false;
static TargetBinding s_targets[kConfigMaxSignals];
static uint8_t s_targetCount = 0;

// A cross-signal expression reads values its own frame does not carry, so it
// runs after every plain signal in the frame has been stored.
bool resolveRefs(const RuntimeSignal &sig, RefValueRs *out) {
    for (uint8_t i = 0; i < sig.refCount; ++i) {
        SignalId sid = SignalIds::kSignalCount;
        for (uint8_t t = 0; t < s_targetCount; ++t) {
            if (s_targets[t].targetId == sig.refTargetIds[i]) {
                sid = s_targets[t].signalId;
                break;
            }
        }
        if (sid >= kSignalStoreMaxSignals || !SignalStore::isValid(sid))
            return false;
        out[i].target_id = sig.refTargetIds[i];
        out[i].value = SignalStore::read(sid);
    }
    return true;
}

void storeReading(SignalId id, float value) {
    if (!std::isfinite(value))
        return;
    SignalStore::update(id, value);
}

void evalCrossSignal(const RuntimeSignal &sig, const uint8_t *data, uint8_t length) {
    RefValueRs refs[kCfgMaxExprRefs] = {};
    if (!resolveRefs(sig, refs))
        return;
    const float val = eval_tokens_refs_rs(
        data, sig.startByte, sig.byteLength, sig.bigEndian, sig.isSigned, sig.bitMask, sig.scale,
        sig.offset, length, s_exprPool + sig.tokenOffset, sig.tokenCount, refs, sig.refCount);
    storeReading(sig.signalId, val);
}

struct ExprRejection {
    int32_t status;
    const char *errorCode;
    const char *reason;
};

constexpr ExprRejection kExprRejections[] = {
    {kCanshiftExprErrLex, "EXPR_LEX", "unknown symbol"},
    {kCanshiftExprErrTooLong, "EXPR_LONG", "too many tokens"},
    {kCanshiftExprErrSyntax, "EXPR_SYNTAX", "syntax error"},
    {kCanshiftExprErrTooDeep, "EXPR_DEPTH", "nested too deep"},
    {kCanshiftExprErrNoRoom, "EXPR_POOL", "expression pool full"},
};

constexpr ExprRejection kUnknownExprRejection = {0, "EXPR_FAIL", "rejected"};

const ExprRejection &exprRejection(int32_t status) {
    const auto *end = std::end(kExprRejections);
    const auto *it = std::find_if(std::begin(kExprRejections), end,
                                  [status](const ExprRejection &r) { return r.status == status; });
    return it == end ? kUnknownExprRejection : *it;
}

void reportExprRejected(const CfgSignalDef &def, int32_t status) {
    const ExprRejection &rejection = exprRejection(status);
    LOG_ERROR("CAN", "signals.json: dropping '%s' — expression %s: %s", def.name, rejection.reason,
              def.expr);
    char msg[sizeof(FwError::message)];
    snprintf(msg, sizeof(msg), "%.32s: %s", def.name, rejection.reason);
    ErrorStore::push(ERROR_SRC_CONFIG, rejection.errorCode, msg);
}

bool compileExpression(const CfgSignalDef &def, RuntimeSignal &r) {
    r.tokenCount = 0;
    const size_t exprLen = strnlen(def.expr, sizeof(def.expr));
    r.hasExpr = exprLen > 0;
    if (!r.hasExpr)
        return true;
    const int32_t status =
        compile_expr_rs(reinterpret_cast<const uint8_t *>(def.expr), exprLen,
                        s_exprPool + s_exprPoolUsed, kExprPoolTokens - s_exprPoolUsed);
    if (status <= 0) {
        reportExprRejected(def, status);
        return false;
    }
    r.tokenOffset = static_cast<uint16_t>(s_exprPoolUsed);
    r.tokenCount = static_cast<uint8_t>(status);
    s_exprPoolUsed += static_cast<size_t>(status);
    return true;
}

void fillRuntimeSignal(const CfgSignalDef &def, SignalId sid, RuntimeSignal &r) {
    r.canFrameId = def.canFrameId;
    r.startByte = def.startByte;
    r.byteLength = def.byteLength;
    r.bigEndian = def.bigEndian;
    r.isSigned = def.isSigned;
    r.scale = def.scale;
    r.offset = def.offset;
    r.bitMask = def.bitMask;
    r.signalId = sid;
    r.refCount = def.exprRefCount;
    for (uint8_t i = 0; i < def.exprRefCount; ++i)
        r.refTargetIds[i] = def.exprRefs[i];
}

void bindTarget(const CfgSignalDef &def, SignalId sid) {
    if (!def.hasTargetId || s_targetCount >= kConfigMaxSignals)
        return;
    s_targets[s_targetCount].targetId = def.targetId;
    s_targets[s_targetCount].signalId = sid;
    ++s_targetCount;
}

bool isPolled(const CfgSignalDef &def) {
    return def.pollIntervalMs != 0;
}

bool addBroadcastDecode(const CfgSignalDef &def, SignalId sid) {
    RuntimeSignal &r = s_runtime[s_runtimeCount];
    fillRuntimeSignal(def, sid, r);
    if (!compileExpression(def, r))
        return false;
    ++s_runtimeCount;
    return true;
}

void addRuntimeSignal(const CfgSignalDef &def) {
    const SignalId sid = signalIdFromName(def.name);
    if (sid == SignalIds::kSignalCount) {
        LOG_WARN("CAN", "Unknown signal name '%s' in signals.json — skipping", def.name);
        return;
    }
    if (!isPolled(def) && !addBroadcastDecode(def, sid))
        return;
    bindTarget(def, sid);
    SignalStore::setTimeout(sid, def.timeoutMs);
}

}

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
                                          s_exprPool + sig.tokenOffset, sig.tokenCount);
        storeReading(sig.signalId, val);
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
    s_exprPoolUsed = 0;
    s_runtimeLoaded = false;

    for (uint8_t i = 0; i < cfg.signalCount && s_runtimeCount < kConfigMaxSignals; ++i)
        addRuntimeSignal(cfg.signals[i]);

    std::sort(
        s_runtime, s_runtime + s_runtimeCount,
        [](const RuntimeSignal &a, const RuntimeSignal &b) { return a.canFrameId < b.canFrameId; });

    s_runtimeLoaded = true;
    LOG_INFO("CAN", "Dynamic signal table loaded: %d signals from signals.json", s_runtimeCount);
}
