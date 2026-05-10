// widget_helpers.cpp — Shared widget drawing helpers.
//
// See widget_helpers.h for the API contract. config_loader.h is included
// here (rather than in the header) so the public surface stays slim and
// widget headers don't transitively pull in the full config layer.

#include "widget_helpers.h"

#include "config/config_loader.h"

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

ThresholdZones resolveZones(float warningLevel, float dangerLevel, float minValue,
                            float maxValue) {
    ThresholdZones z{};
    z.hasWarn = !std::isnan(warningLevel) && warningLevel > minValue;
    z.hasDanger = !std::isnan(dangerLevel) && dangerLevel > minValue && dangerLevel <= maxValue;
    z.warnPct = z.hasWarn ? clampPct(warningLevel, minValue, maxValue) : 1.0f;
    z.dangerPct = z.hasDanger ? clampPct(dangerLevel, minValue, maxValue) : 1.0f;
    return z;
}

uint32_t zoneFillColor(float pct, float warnPct, float dangerPct) {
    if (pct >= dangerPct)
        return kZoneDangerRgb;
    if (pct >= warnPct)
        return kZoneWarningRgb;
    return kZoneNormalRgb;
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
    const char *p = prefix ? prefix : "";
    const char *s = suffix ? suffix : "";
    return snprintf(out, outLen, "%s%.*f%s", p, static_cast<int>(decimals), value, s);
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
