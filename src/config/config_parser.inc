
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

uint8_t clampLayoutTrack(int value, int minValue, int maxValue) {
    const int clamped = value < minValue ? minValue : value > maxValue ? maxValue : value;
    return static_cast<uint8_t>(clamped);
}

void parseWidgetLayout(JsonObjectConst layout, CfgLayout *out) {
    constexpr int kGridTracks = 12;
    out->colSpan = clampLayoutTrack(layout["colSpan"] | 1, 1, kGridTracks);
    out->rowSpan = clampLayoutTrack(layout["rowSpan"] | 1, 1, kGridTracks);
    out->col = clampLayoutTrack(layout["col"] | 0, 0, kGridTracks - out->colSpan);
    out->row = clampLayoutTrack(layout["row"] | 0, 0, kGridTracks - out->rowSpan);
    out->zOrder = layout["zOrder"] | 0;
    out->x = 0;
    out->y = 0;
    out->w = 0;
    out->h = 0;
}

void parseWidgetStyle(JsonObjectConst style, CfgStyle *out) {
    parseColor(style["primaryColor"] | "#FFFFFF", &out->primaryColor);
    parseColor(style["secondaryColor"] | "#333333", &out->secondaryColor);
    parseColor(style["warningColor"] | "#FF8800", &out->warningColor);
    parseColor(style["criticalColor"] | "#FF0000", &out->criticalColor);
    parseColor(style["textColor"] | "#FFFFFF", &out->textColor);
    out->fontSize = style["fontSize"] | 16;
    const char *borderHex = style["borderColor"] | nullptr;
    out->hasBorder = (borderHex != nullptr);
    if (out->hasBorder)
        parseColor(borderHex, &out->borderColor);
    out->respectDayMode = style["respectDayMode"] | true;
}

void parseShiftLightWidget(JsonObjectConst cfg, CfgShiftLightParams *out) {
    out->startValue = cfg["startValue"] | 3000.0f;
    int red = cfg["redSegments"] | 5;
    if (red < 0)
        red = 0;
    if (red > 12)
        red = 12;
    out->redSegments = static_cast<uint8_t>(red);
}

void parseWarningWidget(JsonObjectConst cfg, CfgWarningParams *out) {
    out->invertLogic = cfg["invertLogic"] | false;
    out->threshold = cfg["threshold"] | 0.5f;
    strlcpy(out->iconName, cfg["iconName"] | "", sizeof(out->iconName));
}

void parseButtonColors(JsonObjectConst cfg, CfgButtonParams *out) {
    JsonObjectConst colors = cfg["colors"];
    out->hasColors = !colors.isNull();
    if (out->hasColors) {
        parseColor(colors["normal"] | "#FFFFFF", &out->colorNormal);
        parseColor(colors["active"] | "#FFFFFF", &out->colorActive);
    } else {
        out->colorNormal.rgb = 0x000000;
        out->colorActive.rgb = 0x000000;
    }
}

void parseButtonActionsArray(JsonArrayConst actionsArr, const char *widgetId,
                             CfgButtonParams *out) {
    if (actionsArr.isNull())
        return;
    const size_t total = actionsArr.size();
    if (total > kCfgMaxButtonActions) {
        LOG_WARN("BTN", "button '%s': %u actions exceed CFG_MAX_BUTTON_ACTIONS=%u — extras ignored",
                 widgetId, static_cast<unsigned>(total),
                 static_cast<unsigned>(kCfgMaxButtonActions));
    }
    for (JsonObjectConst a : actionsArr) {
        if (out->actionsCount >= kCfgMaxButtonActions)
            break;
        CfgButtonAction parsed{};
        parseButtonAction(a, &parsed);
        if (parsed.type == CfgButtonActionType::UNKNOWN) {
            LOG_WARN("BTN", "button '%s': unknown action type '%s' — skipped", widgetId,
                     static_cast<const char *>(a["type"] | ""));
            continue;
        }
        out->actions[out->actionsCount++] = parsed;
    }
}

void applyLegacyTargetPageId(JsonObjectConst cfg, CfgButtonParams *out) {
    const char *legacy = cfg["targetPageId"] | "";
    if (legacy[0] == '\0')
        return;
    CfgButtonAction &a = out->actions[0];
    a.type = CfgButtonActionType::NAV_PAGE;
    strlcpy(a.pageId, legacy, sizeof(a.pageId));
    a.mapIndex = 0;
    a.canFrameId = 0;
    a.canDataLen = 0;
    a.canExtended = false;
    memset(a.canData, 0, sizeof(a.canData));
    out->actionsCount = 1;
}

void parseButtonState(JsonObjectConst stateJson, const char *widgetId, uint8_t stateIndex,
                      CfgButtonState *out) {
    strlcpy(out->label, stateJson["label"] | "", kCfgMaxNameLen);

    JsonObjectConst colors = stateJson["colors"];
    out->hasColors = !colors.isNull();
    if (out->hasColors) {
        parseColor(colors["normal"] | "#FFFFFF", &out->colorNormal);
        parseColor(colors["active"] | "#FFFFFF", &out->colorActive);
    } else {
        out->colorNormal.rgb = 0x000000;
        out->colorActive.rgb = 0x000000;
    }

    CfgButtonAction parsed{};
    parseButtonAction(stateJson["action"], &parsed);
    if (parsed.type == CfgButtonActionType::UNKNOWN) {
        LOG_WARN("BTN", "cycle button '%s' state[%u]: unknown action type — coerced to no-op",
                 widgetId, static_cast<unsigned>(stateIndex));
    }
    out->action = parsed;
}

void parseCycleStates(JsonArrayConst statesArr, const char *widgetId, CfgButtonParams *out) {
    out->statesCount = 0;
    if (statesArr.isNull())
        return;
    const size_t total = statesArr.size();
    if (total > kCfgMaxCycleStates) {
        LOG_WARN("BTN",
                 "cycle button '%s': %u states exceed CFG_MAX_CYCLE_STATES=%u — extras ignored",
                 widgetId, static_cast<unsigned>(total), static_cast<unsigned>(kCfgMaxCycleStates));
    }
    for (JsonObjectConst stateJson : statesArr) {
        if (out->statesCount >= kCfgMaxCycleStates)
            break;
        parseButtonState(stateJson, widgetId, out->statesCount, &out->states[out->statesCount]);
        out->statesCount++;
    }
}

void parseButtonWidget(JsonObjectConst cfg, const char *widgetId, CfgButtonParams *out) {
    strlcpy(out->label, cfg["label"] | "", kCfgMaxNameLen);
    strlcpy(out->kicker, cfg["kicker"] | "", kCfgMaxNameLen);
    out->isToggle = cfg["isToggle"] | false;
    out->showLabel = cfg["showLabel"] | true;
    parseButtonColors(cfg, out);

    const char *modeStr = cfg["mode"] | "single";
    out->mode = strcmp(modeStr, "cycle") == 0 ? CfgButtonMode::CYCLE : CfgButtonMode::SINGLE;

    out->actionsCount = 0;
    out->statesCount = 0;
    out->initialActiveIndex = 0;

    if (out->mode == CfgButtonMode::CYCLE) {
        parseCycleStates(cfg["states"], widgetId, out);
        const int initial = cfg["initialActiveIndex"] | 0;
        const uint8_t clamped = out->statesCount == 0 ? 0
                                : initial < 0         ? 0
                                : initial >= out->statesCount
                                    ? static_cast<uint8_t>(out->statesCount - 1)
                                    : static_cast<uint8_t>(initial);
        out->initialActiveIndex = clamped;
        return;
    }

    parseButtonActionsArray(cfg["actions"], widgetId, out);
    if (out->actionsCount == 0)
        applyLegacyTargetPageId(cfg, out);
}

void parseTimerWidget(JsonObjectConst cfg, CfgTimerParams *out) {
    out->big = cfg["big"] | 0;
    out->autoStart = cfg["autoStart"] | false;
    out->formatMsec = strcmp(cfg["format"] | "mm:ss", "ss.mmm") == 0;
    out->source = parseTimerSource(cfg["source"] | "elapsed");
}

void parseImageWidget(JsonObjectConst cfg, CfgImageParams *out) {
    strlcpy(out->imagePath, cfg["imagePath"] | "", kCfgMaxPathLen);
}

void parseLabelWidgetParams(JsonObjectConst cfg, CfgWidget *w) {
    parseLabelParams(cfg, &w->label);
}

void parseWarningParams(JsonObjectConst cfg, CfgWidget *w) {
    parseWarningWidget(cfg, &w->warning);
}

void parseButtonParams(JsonObjectConst cfg, CfgWidget *w) {
    w->button = new (std::nothrow) CfgButtonParams{};
    if (w->button == nullptr) {
        LOG_ERROR("CFG", "button '%s': params alloc failed — widget dropped", w->id);
        ErrorStore::push(ERROR_SRC_CONFIG, "OOM", "button params alloc failed");
        w->type = WidgetType::UNKNOWN;
        return;
    }
    parseButtonWidget(cfg, w->id, w->button);
}

void parseTimerParams(JsonObjectConst cfg, CfgWidget *w) {
    parseTimerWidget(cfg, &w->timer);
}

void parseImageParams(JsonObjectConst cfg, CfgWidget *w) {
    parseImageWidget(cfg, &w->image);
}

void parseGearParams(JsonObjectConst cfg, CfgWidget *w) {
    parseGearWidget(cfg, &w->label);
}

void parseShiftLightParams(JsonObjectConst cfg, CfgWidget *w) {
    parseShiftLightWidget(cfg, &w->shiftLight);
}

struct WidgetParamsParser {
    WidgetType type;
    void (*parse)(JsonObjectConst, CfgWidget *);
};

constexpr WidgetParamsParser kWidgetParamsParsers[] = {
    {WidgetType::GAUGE, parseGaugeWidget},     {WidgetType::LABEL, parseLabelWidgetParams},
    {WidgetType::WARNING, parseWarningParams}, {WidgetType::BUTTON, parseButtonParams},
    {WidgetType::TIMER, parseTimerParams},     {WidgetType::IMAGE, parseImageParams},
    {WidgetType::GEAR_IND, parseGearParams},   {WidgetType::SHIFT_LIGHT, parseShiftLightParams},
};

const WidgetParamsParser *paramsParserFor(WidgetType type) {
    for (const WidgetParamsParser &parser : kWidgetParamsParsers) {
        if (parser.type == type)
            return &parser;
    }
    return nullptr;
}

}

void parseWidget(JsonObjectConst src, CfgWidget *w) {
    strlcpy(w->id, src["id"] | "", kCfgMaxIdLen);
    strlcpy(w->signalId, src["signal"] | "", kCfgMaxSignalLen);
    w->type = parseWidgetType(src["type"] | "");

    parseWidgetLayout(src["layout"], &w->layout);
    parseWidgetStyle(src["style"], &w->style);

    const WidgetParamsParser *parser = paramsParserFor(w->type);
    if (parser != nullptr)
        parser->parse(src["config"], w);
}

}
