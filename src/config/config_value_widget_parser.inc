
#include "config_loader_internal.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

namespace ConfigLoaderInternal {

namespace {

float optionalLevel(JsonVariantConst raw) {
    return raw.is<float>() ? raw.as<float>() : NAN;
}

void parseKicker(JsonObjectConst cfg, char *out, size_t outLen) {
    strlcpy(out, cfg["kicker"] | "", outLen);
}

void parseArcGaugeParams(JsonObjectConst cfg, CfgGaugeParams *out) {
    out->big = cfg["big"] | 0;
    out->minValue = cfg["minValue"] | 0.0f;
    out->maxValue = cfg["maxValue"] | 100.0f;
    out->warnLevel = optionalLevel(cfg["warnLevel"]);
    out->dangerLevel = cfg["dangerLevel"] | 95.0f;
    out->dangerBelow = cfg["dangerBelow"] | false;
    out->revFlash = cfg["revFlash"] | false;
    out->decimalPlaces = cfg["decimalPlaces"] | 0;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    strlcpy(out->iconName, cfg["iconName"] | "", sizeof(out->iconName));
    parseKicker(cfg, out->kicker, sizeof(out->kicker));
}

void parseLabelParams(JsonObjectConst cfg, CfgLabelParams *out) {
    out->big = cfg["big"] | 0;
    out->decimalPlaces = cfg["decimalPlaces"] | 0;
    out->warnLevel = optionalLevel(cfg["warnLevel"]);
    out->dangerLevel = optionalLevel(cfg["dangerLevel"]);
    out->dangerBelow = cfg["dangerBelow"] | false;
    out->showBar = cfg["showBar"] | false;
    out->revFlash = cfg["revFlash"] | false;
    out->minValue = cfg["minValue"] | 0.0f;
    out->maxValue = cfg["maxValue"] | 100.0f;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    parseKicker(cfg, out->kicker, sizeof(out->kicker));
}

void parseGaugeWidget(JsonObjectConst cfg, CfgWidget *w) {
    const char *displayStyle = cfg["displayStyle"] | "arc";
    if (strcmp(displayStyle, "numeric") == 0) {
        w->type = WidgetType::LABEL;
        parseLabelParams(cfg, &w->label);
    } else {
        parseArcGaugeParams(cfg, &w->gauge);
    }
}

void parseGearWidget(JsonObjectConst cfg, CfgLabelParams *out) {
    out->big = cfg["big"] | 0;
    out->decimalPlaces = 0;
    out->warnLevel = NAN;
    out->dangerLevel = NAN;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    parseKicker(cfg, out->kicker, sizeof(out->kicker));
}

}

}
