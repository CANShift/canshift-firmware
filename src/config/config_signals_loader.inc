
#include "config_loader_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "config/parse_utils.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include "json_reader.h"
#include "signals_source.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ConfigLoaderInternal {

namespace {

constexpr uint32_t kDefaultCanSpeedKbps = 500;

uint32_t parseMapSwitchId(JsonVariantConst idv) {
    uint32_t id = 0;
    if (idv.is<uint32_t>()) {
        id = idv.as<uint32_t>();
    } else {
        const char *idStr = idv.as<const char *>();
        if (idStr && !parseU32Strict(idStr, 0, &id)) {
            LOG_WARN("CFG", "signals.json out.map_switch.id='%s' is not a valid number — ignored",
                     idStr);
            id = 0;
        }
    }
    if (id > kCanExtendedIdMax) {
        LOG_WARN("CFG", "signals.json out.map_switch.id=0x%lX exceeds 29-bit max — ignored",
                 static_cast<unsigned long>(id));
        id = 0;
    }
    return id;
}

void parseMapSwitchOut(JsonObjectConst outObj, CfgSignalsOut &out) {
    out.mapSwitchFrameId = 0;
    out.mapSwitchExtended = false;
    if (outObj.isNull())
        return;
    JsonObjectConst mapSwitch = outObj["map_switch"];
    if (mapSwitch.isNull())
        return;
    const uint32_t id = parseMapSwitchId(mapSwitch["id"]);
    out.mapSwitchFrameId = id;
    const bool extendedFlag = mapSwitch["extended"] | false;
    out.mapSwitchExtended = extendedFlag || (id > kCanStandardIdMax);
}

bool parseSignalIdentity(JsonObjectConst sig, CfgSignalDef *s) {
    const char *rawName = sig["name"] | "";
    const size_t nameLen = strlcpy(s->name, rawName, kCfgMaxSignalLen);
    if (nameLen >= kCfgMaxSignalLen) {
        LOG_WARN("CFG", "signals.json: dropping signal — name too long (%u chars, max %u): '%s...'",
                 static_cast<unsigned>(nameLen), static_cast<unsigned>(kCfgMaxSignalLen - 1),
                 s->name);
        return false;
    }
    const char *raw = sig["canFrameId"] | (const char *)nullptr;
    if (!parseU32Strict(raw, 16, &s->canFrameId)) {
        LOG_WARN("CFG", "signals.json: dropping '%s' — invalid canFrameId '%s'", s->name,
                 raw ? raw : "(null)");
        return false;
    }
    if (s->canFrameId > kCanExtendedIdMax) {
        LOG_WARN("CFG", "signals.json: dropping '%s' — canFrameId 0x%lX exceeds 29-bit max",
                 s->name, static_cast<unsigned long>(s->canFrameId));
        return false;
    }
    return true;
}

void parseSignalScaling(JsonObjectConst sig, CfgSignalDef *s) {
    s->startByte = sig["startByte"] | 0;
    s->byteLength = sig["byteLength"] | 1;
    s->bigEndian = sig["bigEndian"] | true;
    s->isSigned = sig["signed"] | false;
    s->scale = sig["scale"] | 1.0f;
    s->offset = sig["offset"] | 0.0f;
    const char *rawUnit = sig["unit"] | "";
    const size_t unitLen = strlcpy(s->unit, rawUnit, sizeof(s->unit));
    if (unitLen >= sizeof(s->unit)) {
        LOG_WARN("CFG", "signals.json: '%s' unit too long (%u chars, max %u) — unit reset to \"\"",
                 s->name, static_cast<unsigned>(unitLen),
                 static_cast<unsigned>(sizeof(s->unit) - 1));
        s->unit[0] = '\0';
    }
    s->minValue = sig["min"] | 0.0f;
    s->maxValue = sig["max"] | 100.0f;
    JsonVariantConst wv = sig["warningLevel"];
    s->warningLevel = wv.isNull() ? NAN : wv.as<float>();
    JsonVariantConst dv = sig["dangerLevel"];
    s->dangerLevel = dv.isNull() ? NAN : dv.as<float>();
    JsonVariantConst hwv = sig["highWarningLevel"];
    s->highWarningLevel = hwv.isNull() ? NAN : hwv.as<float>();
    JsonVariantConst hdv = sig["highDangerLevel"];
    s->highDangerLevel = hdv.isNull() ? NAN : hdv.as<float>();
    s->timeoutMs = sig["timeoutMs"] | kSignalDefaultTimeoutMs;
}

void parseSignalBitMask(JsonObjectConst sig, CfgSignalDef *s) {
    const char *bitMaskStr = sig["bitMask"] | (const char *)nullptr;
    if (!bitMaskStr) {
        s->bitMask = 0;
        return;
    }
    uint32_t maskVal = 0;
    if (parseU32Strict(bitMaskStr, 16, &maskVal)) {
        s->bitMask = static_cast<uint8_t>(maskVal);
        return;
    }
    LOG_WARN("CFG", "signals.json: '%s' bitMask='%s' is not valid hex — using 0 (no mask)", s->name,
             bitMaskStr);
    s->bitMask = 0;
}

void parseSignalExprRefs(JsonObjectConst sig, CfgSignalDef *s) {
    s->exprRefCount = 0;
    JsonArrayConst refs = sig["exprRefs"];
    if (refs.isNull())
        return;
    if (refs.size() > kCfgMaxExprRefs) {
        LOG_WARN("CFG", "signal '%s': %u exprRefs exceed CFG_MAX_EXPR_REFS=%d — expression dropped",
                 s->name, static_cast<unsigned>(refs.size()), kCfgMaxExprRefs);
        s->expr[0] = '\0';
        return;
    }
    for (JsonVariantConst ref : refs) {
        const uint32_t id = ref | 0u;
        if (id > UINT16_MAX) {
            LOG_WARN("CFG", "signal '%s': exprRef %lu out of range — expression dropped", s->name,
                     static_cast<unsigned long>(id));
            s->expr[0] = '\0';
            s->exprRefCount = 0;
            return;
        }
        s->exprRefs[s->exprRefCount++] = static_cast<uint16_t>(id);
    }
}

void parseSignalExpr(JsonObjectConst sig, CfgSignalDef *s) {
    const uint32_t targetId = sig["targetId"] | 0xFFFFFFFFu;
    s->hasTargetId = targetId <= UINT16_MAX;
    s->targetId = s->hasTargetId ? static_cast<uint16_t>(targetId) : 0;

    const char *exprStr = sig["expr"] | (const char *)nullptr;
    if (!exprStr) {
        s->expr[0] = '\0';
        s->exprRefCount = 0;
        return;
    }
    strlcpy(s->expr, exprStr, sizeof(s->expr));
    parseSignalExprRefs(sig, s);
}

void parseSignalPolling(JsonObjectConst sig, CfgSignalDef *s) {
    s->pollMode = 0;
    s->pollPid = 0;
    s->pollIntervalMs = 0;
    JsonObjectConst pollObj = sig["polling"];
    if (pollObj.isNull())
        return;
    const uint32_t mode = pollObj["mode"] | 0u;
    const uint32_t pid = pollObj["pid"] | 0u;
    const uint32_t intervalMs = pollObj["intervalMs"] | 0u;
    const bool modeOk = (mode == 0x01u);
    const bool pidOk = (pid <= 0xFFu);
    const bool intervalOk =
        (intervalMs >= kObd2MinIntervalMsFw && intervalMs <= kObd2MaxIntervalMsFw);
    if (modeOk && pidOk && intervalOk) {
        s->pollMode = static_cast<uint8_t>(mode);
        s->pollPid = static_cast<uint8_t>(pid);
        s->pollIntervalMs = intervalMs;
        return;
    }
    LOG_WARN("CFG",
             "signals.json: '%s' polling block invalid "
             "(mode=0x%02X pid=0x%02X intervalMs=%u) — broadcast fallback",
             s->name, static_cast<unsigned>(mode), static_cast<unsigned>(pid),
             static_cast<unsigned>(intervalMs));
}

bool signalLayoutValid(const CfgSignalDef &s) {
    const bool byteLenValid =
        (s.byteLength == 1 || s.byteLength == 2 || s.byteLength == 3 || s.byteLength == 4);
    const bool startInRange = (s.startByte < kCanFrameMaxBytes);
    const bool fitsInFrame =
        (static_cast<uint16_t>(s.startByte) + static_cast<uint16_t>(s.byteLength) <=
         kCanFrameMaxBytes);
    if (byteLenValid && startInRange && fitsInFrame)
        return true;
    LOG_WARN("CFG", "signals.json: dropping '%s' (startByte=%u byteLength=%u) — out of range",
             s.name, s.startByte, s.byteLength);
    return false;
}

bool parseSignalDef(JsonObjectConst sig, CfgSignalDef *s) {
    if (!parseSignalIdentity(sig, s))
        return false;
    parseSignalScaling(sig, s);
    parseSignalBitMask(sig, s);
    parseSignalExpr(sig, s);
    parseSignalPolling(sig, s);
    return signalLayoutValid(*s);
}

struct SignalListTally {
    uint16_t accepted;
    uint16_t rejected;
    uint16_t overCap;
};

void keepSignal(CfgSignalConfig *into, const CfgSignalDef &def, uint16_t index) {
    if (into == nullptr)
        return;
    into->signals[index] = def;
    into->signalCount = static_cast<uint8_t>(index + 1);
}

SignalListTally parseSignalList(JsonArrayConst signals, CfgSignalConfig *into) {
    SignalListTally tally = {};
    if (into != nullptr)
        into->signalCount = 0;
    for (JsonObjectConst sig : signals) {
        if (tally.accepted >= kConfigMaxSignals) {
            ++tally.overCap;
            continue;
        }
        CfgSignalDef tmp{};
        if (!parseSignalDef(sig, &tmp)) {
            ++tally.rejected;
            continue;
        }
        keepSignal(into, tmp, tally.accepted++);
    }
    return tally;
}

void reportOverCap(const SignalListTally &tally) {
    if (tally.overCap == 0)
        return;
    LOG_ERROR("CFG", "signals.json: %u signals past the %u cap were dropped",
              static_cast<unsigned>(tally.overCap), static_cast<unsigned>(kConfigMaxSignals));
    ErrorStore::push(ERROR_SRC_CONFIG, "SIG_CAP", "signals.json exceeds the signal cap");
}

SignalsSource::Verdict verdictOf(const SignalListTally &tally) {
    if (tally.overCap > 0)
        return SignalsSource::Verdict::TooMany;
    if (tally.rejected > 0)
        return SignalsSource::Verdict::SignalRejected;
    return SignalsSource::Verdict::Ok;
}

void fillSignalHeader(JsonDocument &doc, CfgSignalConfig &into) {
    strlcpy(into.version, doc["version"] | "", sizeof(into.version));
    checkSchemaVersion("signals.json", into.version);
    strlcpy(into.protocol, doc["protocol"] | "", sizeof(into.protocol));
    into.canSpeedKbps = doc["canSpeedKbps"] | kDefaultCanSpeedKbps;
    parseMapSwitchOut(doc["out"], into.out);
}

} // namespace

SignalsSource::Verdict parseSignalsDocument(JsonDocument &doc, CfgSignalConfig *into) {
    JsonArrayConst list = doc["signals"].as<JsonArrayConst>();
    if (list.isNull())
        return SignalsSource::Verdict::NoSignalList;
    if (into == nullptr)
        return verdictOf(parseSignalList(list, nullptr));
    fillSignalHeader(doc, *into);
    const SignalListTally tally = parseSignalList(list, into);
    reportOverCap(tally);
    into->loaded = true;
    LOG_INFO("CFG", "signals.json loaded: %d signals", into->signalCount);
    return verdictOf(tally);
}

} // namespace ConfigLoaderInternal
