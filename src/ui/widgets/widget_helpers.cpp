// widget_helpers.cpp — Shared widget drawing helpers.
//
// See widget_helpers.h for the API contract. config_loader.h is included
// here (rather than in the header) so the public surface stays slim and
// widget headers don't transitively pull in the full config layer.

#include "widget_helpers.h"

#include "config/config_loader.h"
#include "util/format_float.h"

#include <cmath>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace WidgetHelpers {

float clampPct(float value, float minValue, float maxValue) {
    if (maxValue <= minValue)
        return 0.0f;
    float pct = (value - minValue) / (maxValue - minValue);
    if (pct < 0.0f)
        return 0.0f;
    if (pct > 1.0f)
        return 1.0f;
    return pct;
}

ThresholdZones resolveZones(float warningLevel, float dangerLevel, float minValue, float maxValue) {
    ThresholdZones z{};
    z.hasWarn = !std::isnan(warningLevel) && warningLevel > minValue;
    z.hasDanger = !std::isnan(dangerLevel) && dangerLevel > minValue && dangerLevel <= maxValue;
    z.warnPct = z.hasWarn ? clampPct(warningLevel, minValue, maxValue) : 1.0f;
    z.dangerPct = z.hasDanger ? clampPct(dangerLevel, minValue, maxValue) : 1.0f;
    return z;
}

static uint32_t lerpRgb(uint32_t a, uint32_t b, float t) {
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    auto ch = [](uint32_t c, int s) { return static_cast<int>((c >> s) & 0xFFu); };
    auto mix = [t](int x, int y) {
        return static_cast<uint32_t>(static_cast<float>(x) +
                                     (static_cast<float>(y) - static_cast<float>(x)) * t);
    };
    return (mix(ch(a, 16), ch(b, 16)) << 16) | (mix(ch(a, 8), ch(b, 8)) << 8) |
           mix(ch(a, 0), ch(b, 0));
}

uint32_t zoneFillColor(float pct, float warnPct, float dangerPct) {
    if (pct >= dangerPct)
        return kZoneDangerRgb;
    if (pct >= warnPct)
        return kZoneWarningRgb;
    return kZoneNormalRgb;
}

uint32_t zoneFillColorSmooth(float pct, float warnPct, float dangerPct) {
    if (warnPct > 1.0f)
        return kZoneNormalRgb;
    if (pct < warnPct)
        return kZoneNormalRgb;
    if (dangerPct <= 1.0f) {
        if (pct >= dangerPct) {
            const float range = 1.0f - dangerPct;
            return lerpRgb(kZoneWarningRgb, kZoneDangerRgb,
                           range > 0.0f ? (pct - dangerPct) / range : 1.0f);
        }
        const float range = dangerPct - warnPct;
        return lerpRgb(kZoneNormalRgb, kZoneWarningRgb,
                       range > 0.0f ? (pct - warnPct) / range : 1.0f);
    }
    const float range = 1.0f - warnPct;
    return lerpRgb(kZoneNormalRgb, kZoneWarningRgb, range > 0.0f ? (pct - warnPct) / range : 1.0f);
}

void formatSignalLabel(const char *src, char *out, size_t outLen) {
    if (outLen == 0)
        return;
    if (!src || src[0] == '\0') {
        strlcpy(out, "-", outLen);
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < outLen; ++i) {
        char c = src[i];
        if (c == '_')
            c = ' ';
        out[j++] = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    }
    out[j] = '\0';
}

int formatValue(char *out, size_t outLen, const char *prefix, uint8_t decimals, float value,
                const char *suffix) {
    if (!out || outLen == 0)
        return 0;
    out[0] = '\0';
    // Firmware overrides newlib's float printf with the integer-only variant
    // (see util/no_float_printf.c) — any `%f` in a runtime format string is
    // mis-parsed and corrupts va_args, which previously NULL-deref'd in
    // strlen on the next `%s`. Compose the string manually via FloatFormat.
    const char *p = prefix ? prefix : "";
    const char *s = suffix ? suffix : "";

    size_t pos = 0;
    const size_t prefixLen = strlen(p);
    const size_t copyPrefix = (prefixLen < outLen - 1) ? prefixLen : outLen - 1;
    memcpy(out + pos, p, copyPrefix);
    pos += copyPrefix;
    out[pos] = '\0';

    if (pos + 1 < outLen) {
        FloatFormat::formatFixed(out + pos, outLen - pos, value, static_cast<int>(decimals));
        pos += strlen(out + pos);
    }

    if (pos + 1 < outLen) {
        const size_t suffixLen = strlen(s);
        const size_t copySuffix = (suffixLen < outLen - 1 - pos) ? suffixLen : outLen - 1 - pos;
        memcpy(out + pos, s, copySuffix);
        pos += copySuffix;
        out[pos] = '\0';
    }
    return static_cast<int>(pos);
}

bool setLabelTextIfChanged(lv_obj_t *label, const char *text) {
    if (!label || !text)
        return false;
    const char *current = lv_label_get_text(label);
    if (current != nullptr && strcmp(current, text) == 0)
        return false;
    lv_label_set_text(label, text);
    return true;
}

const CfgColorRamp *resolveSignalRamp(const char *signalId) {
    const CfgSignalDef *def = ConfigLoader::findSignal(signalId);
    const CfgColorRampDef empty{};
    const CfgColorRampDef &perSignal = def ? def->colorRamp : empty;
    return resolveRamp(perSignal, signalId);
}

const char *resolveDisplayUnit(const char *signalId, const char *configSuffix) {
    // Explicit per-widget override wins so existing dashboards keep their
    // hand-picked unit string (e.g. "MPH" instead of the signal's "km/h").
    if (configSuffix && configSuffix[0] != '\0')
        return configSuffix;
    if (!signalId || signalId[0] == '\0')
        return "";
    const CfgSignalDef *def = ConfigLoader::findSignal(signalId);
    return (def && def->unit[0] != '\0') ? def->unit : "";
}

void initContainer(lv_obj_t *cont, const CfgWidget &cfg, int16_t yOffset, bool hasBorder,
                   uint32_t borderRgb) {
    if (!cont)
        return;
    lv_obj_set_pos(cont, cfg.layout.x, cfg.layout.y + yOffset);
    lv_obj_set_size(cont, cfg.layout.w, cfg.layout.h);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    WidgetStyles::applyContainerBase(cont, hasBorder, borderRgb);
}

} // namespace WidgetHelpers
