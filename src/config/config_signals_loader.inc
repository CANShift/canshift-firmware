
#include "config_loader_internal.h"

#include "app_config.h"
#include "board_config.h"
#include "config/parse_utils.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include "json_reader.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ConfigLoaderInternal {

namespace {

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

void parseMapSwitchOut(JsonObjectConst outObj) {
    s_signals.out.mapSwitchFrameId = 0;
    s_signals.out.mapSwitchExtended = false;
    if (outObj.isNull())
        return;
    JsonObjectConst mapSwitch = outObj["map_switch"];
    if (mapSwitch.isNull())
        return;
    const uint32_t id = parseMapSwitchId(mapSwitch["id"]);
    s_signals.out.mapSwitchFrameId = id;
    const bool extendedFlag = mapSwitch["extended"] | false;
    s_signals.out.mapSwitchExtended = extendedFlag || (id > kCanStandardIdMax);
}

bool parseSignalIdentity(JsonObjectConst sig, CfgSignalDef *s) {
    const char *rawName = sig["name"] | "";
    const size_t nameLen = strlcpy(s->name, rawName, CFG_MAX_SIGNAL_LEN);
    if (nameLen >= CFG_MAX_SIGNAL_LEN) {
        LOG_WARN("CFG", "signals.json: dropping signal — name too long (%u chars, max %u): '%s...'",
                 static_cast<unsigned>(nameLen), static_cast<unsigned>(CFG_MAX_SIGNAL_LEN - 1),
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
    s->timeoutMs = sig["timeoutMs"] | SIGNAL_DEFAULT_TIMEOUT_MS;
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
    if (refs.size() > CFG_MAX_EXPR_REFS) {
        LOG_WARN("CFG", "signal '%s': %u exprRefs exceed CFG_MAX_EXPR_REFS=%d — expression dropped",
                 s->name, static_cast<unsigned>(refs.size()), CFG_MAX_EXPR_REFS);
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
        (intervalMs >= OBD2_MIN_INTERVAL_MS_FW && intervalMs <= OBD2_MAX_INTERVAL_MS_FW);
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

void parseSignalList(JsonArrayConst signals) {
    s_signals.signalCount = 0;
    uint16_t overCap = 0;
    for (JsonObjectConst sig : signals) {
        if (s_signals.signalCount >= CONFIG_MAX_SIGNALS) {
            ++overCap;
            continue;
        }
        CfgSignalDef tmp{};
        if (!parseSignalDef(sig, &tmp))
            continue;
        s_signals.signals[s_signals.signalCount++] = tmp;
    }
    if (overCap == 0)
        return;
    LOG_ERROR("CFG", "signals.json: %u signals past the %u cap were dropped",
              static_cast<unsigned>(overCap), static_cast<unsigned>(CONFIG_MAX_SIGNALS));
    ErrorStore::push(ERROR_SRC_CONFIG, "SIG_CAP", "signals.json exceeds the signal cap");
}

} // namespace

bool loadSignals() {
    uint8_t *snapshot = acquireRollbackSnapshot();
    if (snapshot) {
        memcpy(snapshot, &s_signals, sizeof(CfgSignalConfig));
    }

    JsonDocument doc(JsonReader::configAllocator());
    if (!readAndParseWithBak(CONFIG_PATH_SIGNALS, doc)) {
        LOG_ERROR("CFG", "signals.json unreadable (primary + .bak)");
        ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "signals.json unreadable");
        if (snapshot) {
            memcpy(&s_signals, snapshot, sizeof(CfgSignalConfig));
        }
        return false;
    }

    strlcpy(s_signals.version, doc["version"] | "", sizeof(s_signals.version));
    checkSchemaVersion("signals.json", s_signals.version);
    strlcpy(s_signals.protocol, doc["protocol"] | "", sizeof(s_signals.protocol));
    s_signals.canSpeedKbps = doc["canSpeedKbps"] | 500;

    parseMapSwitchOut(doc["out"]);
    parseSignalList(doc["signals"]);

    s_signals.loaded = true;
    LOG_INFO("CFG", "signals.json loaded: %d signals", s_signals.signalCount);
    return true;
}

} // namespace ConfigLoaderInternal
