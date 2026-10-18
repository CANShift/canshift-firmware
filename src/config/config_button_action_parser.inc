
#include "config_loader_internal.h"

#include "app_config.h"
#include "diag/logger.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ConfigLoaderInternal {

namespace {

constexpr int kByteMax = 255;

using ActionParser = void (*)(JsonObjectConst src, CfgButtonAction *out);

struct ActionParserEntry {
    CfgButtonActionType type;
    ActionParser parse;
};

uint8_t clampToByte(int value) {
    if (value < 0)
        return 0;
    if (value > kByteMax)
        return kByteMax;
    return static_cast<uint8_t>(value);
}

void resetButtonAction(CfgButtonAction *out) {
    out->pageId[0] = '\0';
    out->mapIndex = 0;
    out->canFrameId = 0;
    out->canDataLen = 0;
    out->canDataOffLen = 0;
    out->canExtended = false;
    out->cruiseOp = CfgCruiseOp::UNKNOWN;
    out->cruiseStepKmh = 0;
    memset(out->canData, 0, sizeof(out->canData));
    memset(out->canDataOff, 0, sizeof(out->canDataOff));
}

void parseNavPage(JsonObjectConst src, CfgButtonAction *out) {
    strlcpy(out->pageId, src["pageId"] | "", sizeof(out->pageId));
}

void parseMapSwitch(JsonObjectConst src, CfgButtonAction *out) {
    out->mapIndex = clampToByte(src["mapIndex"] | 0);
}

uint32_t parseCanFrameId(JsonVariantConst fv) {
    if (fv.is<uint32_t>())
        return fv.as<uint32_t>();
    const char *fs = fv.as<const char *>();
    return fs ? static_cast<uint32_t>(strtoul(fs, nullptr, 0)) : 0;
}

bool parseCanFrame(JsonObjectConst src, CfgButtonAction *out) {
    out->canFrameId = parseCanFrameId(src["frameId"]);
    const bool extendedFlag = src["extended"] | false;
    out->canExtended = extendedFlag || out->canFrameId > kCanStandardIdMax;
    if (out->canFrameId <= kCanExtendedIdMax)
        return true;
    LOG_WARN("CFG", "can_raw: frameId=0x%lX exceeds 29-bit max — action dropped",
             static_cast<unsigned long>(out->canFrameId));
    out->type = CfgButtonActionType::UNKNOWN;
    out->canFrameId = 0;
    out->canExtended = false;
    return false;
}

void parseCanData(JsonObjectConst src, CfgButtonAction *out) {
    const char *hex = src["data"] | "";
    if (decodeHexBytes(hex, out->canData, sizeof(out->canData), &out->canDataLen))
        return;
    LOG_WARN("CFG", "can_raw: invalid data='%s' (must be ≤16 even-length hex chars)", hex);
    out->type = CfgButtonActionType::UNKNOWN;
    out->canDataLen = 0;
    memset(out->canData, 0, sizeof(out->canData));
}

void parseCanDataOff(JsonObjectConst src, CfgButtonAction *out) {
    const char *hexOff = src["dataOff"] | "";
    if (hexOff[0] == '\0')
        return;
    if (decodeHexBytes(hexOff, out->canDataOff, sizeof(out->canDataOff), &out->canDataOffLen))
        return;
    LOG_WARN("CFG",
             "can_raw: invalid dataOff='%s' (must be ≤16 even-length hex chars) — "
             "disarm frame suppressed",
             hexOff);
    out->canDataOffLen = 0;
    memset(out->canDataOff, 0, sizeof(out->canDataOff));
}

void parseCanRaw(JsonObjectConst src, CfgButtonAction *out) {
    if (!parseCanFrame(src, out))
        return;
    parseCanData(src, out);
    parseCanDataOff(src, out);
}

void parseTimerControl(JsonObjectConst src, CfgButtonAction *out) {
    const char *opStr = src["op"] | "";
    out->timerOp = parseTimerOp(opStr);
    if (out->timerOp != CfgTimerOp::UNKNOWN)
        return;
    LOG_WARN("CFG", "timer_control: unknown op='%s' — action dropped", opStr);
    out->type = CfgButtonActionType::UNKNOWN;
}

void parseCruiseControl(JsonObjectConst src, CfgButtonAction *out) {
    const char *opStr = src["op"] | "";
    out->cruiseOp = parseCruiseOp(opStr);
    if (out->cruiseOp == CfgCruiseOp::UNKNOWN) {
        LOG_WARN("CFG", "cruise_control: unknown op='%s' — action dropped", opStr);
        out->type = CfgButtonActionType::UNKNOWN;
        return;
    }
    out->cruiseStepKmh = clampToByte(src["stepKmh"] | 0);
}

constexpr ActionParserEntry kActionParsers[] = {
    {CfgButtonActionType::NAV_PAGE, parseNavPage},
    {CfgButtonActionType::MAP_SWITCH, parseMapSwitch},
    {CfgButtonActionType::CAN_RAW, parseCanRaw},
    {CfgButtonActionType::TIMER_CONTROL, parseTimerControl},
    {CfgButtonActionType::CRUISE_CONTROL, parseCruiseControl},
};

}

void parseButtonAction(JsonObjectConst src, CfgButtonAction *out) {
    const char *type = src["type"] | "";
    out->type = parseButtonActionType(type);
    resetButtonAction(out);
    for (const ActionParserEntry &entry : kActionParsers) {
        if (entry.type == out->type) {
            entry.parse(src, out);
            return;
        }
    }
}

}
