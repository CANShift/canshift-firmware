#include "widget_helpers.h"

#include "config/config_loader.h"
#include "ui/screen_profile.h"
#include "util/format_float.h"

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

    const int16_t px = ScreenProfile::scaleXVal(cfg.layout.x);
    const int16_t py = static_cast<int16_t>(ScreenProfile::scaleYVal(cfg.layout.y) + yOffset);
    lv_obj_set_pos(cont, px, py);
    lv_obj_set_size(cont, ScreenProfile::scaleXVal(cfg.layout.w),
                    ScreenProfile::scaleYVal(cfg.layout.h));
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    WidgetStyles::applyContainerBase(cont, hasBorder, borderRgb);
}

} // namespace WidgetHelpers
