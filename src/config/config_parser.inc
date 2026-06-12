// config_parser.cpp — Per-type JSON → CfgX struct parsers shared by the file
// loaders in `config_file_loaders.cpp` (#1207).
//
// Extracted from `config_loader.cpp`. Pure transformations: every helper here
// reads from a JSON node and writes into a caller-owned CfgX struct without
// touching the shared `s_dashboard` / `s_signals` / etc. storage or doing any
// I/O. The file loaders drive the per-widget / per-signal walks.

#include "config_loader_internal.h"

#include "app_config.h"
#include "diag/logger.h"

#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace ConfigLoaderInternal {

// ---------------------------------------------------------------------------
// Button-action parser — shared between dashboard buttons and input bindings
// ---------------------------------------------------------------------------

void parseButtonAction(JsonObjectConst src, CfgButtonAction *out) {
    const char *category = src["category"] | "";
    const char *type = src["type"] | "";
    out->type = parseButtonActionType(category, type);
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

    switch (out->type) {
        case CfgButtonActionType::NAV_PAGE:
            strlcpy(out->pageId, src["pageId"] | "", sizeof(out->pageId));
            break;
        case CfgButtonActionType::MAP_SWITCH: {
            int idx = src["mapIndex"] | 0;
            if (idx < 0)
                idx = 0;
            if (idx > 255)
                idx = 255;
            out->mapIndex = static_cast<uint8_t>(idx);
            break;
        }
        case CfgButtonActionType::CAN_RAW: {
            // frameId may arrive as int or hex string in studio writes; ArduinoJson
            // surfaces both as the JSON variant — read as uint32_t when numeric,
            // otherwise parse the string.
            JsonVariantConst fv = src["frameId"];
            if (fv.is<uint32_t>()) {
                out->canFrameId = fv.as<uint32_t>();
            } else {
                const char *fs = fv.as<const char *>();
                out->canFrameId = fs ? static_cast<uint32_t>(strtoul(fs, nullptr, 0)) : 0;
            }
            // Extended (29-bit) ID flag — issue #319. Optional; default false.
            // Auto-promote to extended when the configured ID exceeds the
            // standard 11-bit range so legacy configs that omit the flag but
            // use a >0x7FF ID still transmit a valid frame.
            const bool extendedFlag = src["extended"] | false;
            const bool needsExtended = out->canFrameId > kCanStandardIdMax;
            out->canExtended = extendedFlag || needsExtended;
            if (out->canFrameId > kCanExtendedIdMax) {
                LOG_WARN("CFG", "can_raw: frameId=0x%lX exceeds 29-bit max — action dropped",
                         static_cast<unsigned long>(out->canFrameId));
                out->type = CfgButtonActionType::UNKNOWN;
                out->canFrameId = 0;
                out->canExtended = false;
                break;
            }
            // Decode hex payload up to 8 bytes. Empty string is allowed and
            // yields canDataLen=0 (legal DLC=0 frame). Anything malformed or
            // oversized is demoted to UNKNOWN so the caller skips this action.
            const char *hex = src["data"] | "";
            if (!decodeHexBytes(hex, out->canData, sizeof(out->canData), &out->canDataLen)) {
                LOG_WARN("CFG", "can_raw: invalid data='%s' (must be ≤16 even-length hex chars)",
                         hex);
                out->type = CfgButtonActionType::UNKNOWN;
                out->canDataLen = 0;
                memset(out->canData, 0, sizeof(out->canData));
            }
            // Optional disarm payload (toggle-off). Missing = no disarm frame sent.
            const char *hexOff = src["dataOff"] | "";
            if (hexOff[0] != '\0') {
                if (!decodeHexBytes(hexOff, out->canDataOff, sizeof(out->canDataOff),
                                    &out->canDataOffLen)) {
                    LOG_WARN("CFG",
                             "can_raw: invalid dataOff='%s' (must be ≤16 even-length hex chars) — "
                             "disarm frame suppressed",
                             hexOff);
                    out->canDataOffLen = 0;
                    memset(out->canDataOff, 0, sizeof(out->canDataOff));
                }
            }
            break;
        }
        case CfgButtonActionType::CRUISE_CONTROL: {
            const char *opStr = src["op"] | "";
            out->cruiseOp = parseCruiseOp(opStr);
            if (out->cruiseOp == CfgCruiseOp::UNKNOWN) {
                LOG_WARN("CFG", "cruise_control: unknown op='%s' — action dropped", opStr);
                out->type = CfgButtonActionType::UNKNOWN;
                break;
            }
            int step = src["stepKmh"] | 0;
            if (step < 0)
                step = 0;
            if (step > 255)
                step = 255;
            out->cruiseStepKmh = static_cast<uint8_t>(step);
            break;
        }
        case CfgButtonActionType::UNKNOWN:
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Color ramp — used by the signals loader
// ---------------------------------------------------------------------------

void parseColorRamp(JsonObjectConst src, const char *signalName, CfgColorRampDef *out) {
    out->count = 0;
    out->interpolate = CfgRampInterp::Linear;
    if (src.isNull())
        return;

    const char *interp = src["interpolate"] | "linear";
    out->interpolate = (strcmp(interp, "step") == 0) ? CfgRampInterp::Step : CfgRampInterp::Linear;

    JsonArrayConst stops = src["stops"];
    if (stops.isNull())
        return;

    const size_t total = stops.size();
    if (total > CFG_MAX_RAMP_STOPS) {
        LOG_WARN(
            "CFG", "signal '%s': colorRamp has %u stops > CFG_MAX_RAMP_STOPS=%u — extras dropped",
            signalName, static_cast<unsigned>(total), static_cast<unsigned>(CFG_MAX_RAMP_STOPS));
    }
    for (JsonObjectConst stop : stops) {
        if (out->count >= CFG_MAX_RAMP_STOPS)
            break;
        CfgRampStopDef &dst = out->stops[out->count];
        dst.value = stop["value"] | 0.0f;
        const char *color = stop["color"] | "#000000";
        dst.color = parseHexColorValue(color);
        ++out->count;
    }
    // A single-stop ramp is meaningless — drop it so the renderer falls back
    // to the default lookup. The validator rejects this in studio, but we
    // stay defensive against hand-edited JSON.
    if (out->count < 2) {
        out->count = 0;
    }
}

// ---------------------------------------------------------------------------
// Widget tree (private helpers in anon namespace, dispatcher exported)
// ---------------------------------------------------------------------------

namespace {

// alertThreshold: optional NaN sentinel = disabled (issue #133). Several
// widget config blocks read the same field — factor it out so the per-type
// parsers stay focused on the fields unique to each widget.
float readAlertThreshold(JsonObjectConst cfg) {
    return cfg["alertThreshold"].is<float>() ? cfg["alertThreshold"].as<float>() : NAN;
}

void parseWidgetLayout(JsonObjectConst layout, CfgLayout *out) {
    out->x = layout["x"] | 0;
    out->y = layout["y"] | 0;
    out->w = layout["w"] | 80;
    out->h = layout["h"] | 60;
    out->zOrder = layout["zOrder"] | 0;
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
    // respectDayMode default = true preserves the v0.7.0 contract (#171).
    // Absent or non-bool JSON falls back to true. Issue #191.
    out->respectDayMode = style["respectDayMode"] | true;
}

void parseArcGaugeParams(JsonObjectConst cfg, CfgGaugeParams *out) {
    out->minValue = cfg["minValue"] | 0.0f;
    out->maxValue = cfg["maxValue"] | 100.0f;
    out->dangerLevel = cfg["dangerLevel"] | 95.0f;
    out->alertThreshold = readAlertThreshold(cfg);
    out->showArc = cfg["showArc"] | true;
    out->revFlash = cfg["revFlash"] | false;
    out->decimalPlaces = cfg["decimalPlaces"] | 0;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    out->arcFillStyle = parseArcFillStyle(cfg["arcFillStyle"] | "zones");
    strlcpy(out->iconName, cfg["iconName"] | "", sizeof(out->iconName));
}

void parseLabelParams(JsonObjectConst cfg, CfgLabelParams *out, float alertThreshold) {
    out->decimalPlaces = cfg["decimalPlaces"] | 0;
    out->alertThreshold = alertThreshold;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
}

// Gauge widgets in dashboard.json use displayStyle "numeric" / "arc" to encode
// sub-types — reclassify the outgoing WidgetType here so downstream renderers
// can stay simple and dispatch on a single enum.
void parseGaugeWidget(JsonObjectConst cfg, CfgWidget *w) {
    const char *displayStyle = cfg["displayStyle"] | "arc";
    const float alertThreshold = readAlertThreshold(cfg);
    if (strcmp(displayStyle, "numeric") == 0) {
        // Large numeric readout — render as a label widget.
        w->type = WidgetType::LABEL;
        parseLabelParams(cfg, &w->label, alertThreshold);
    } else {
        // "arc" or unrecognised — arc gauge keeps WidgetType::GAUGE.
        parseArcGaugeParams(cfg, &w->gauge);
    }
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

// Walk the `actions[]` JSON array, drop UNKNOWN entries with a warning, and
// cap at CFG_MAX_BUTTON_ACTIONS. `widgetId` is only used for log context.
void parseButtonActionsArray(JsonArrayConst actionsArr, const char *widgetId,
                             CfgButtonParams *out) {
    if (actionsArr.isNull())
        return;
    const size_t total = actionsArr.size();
    if (total > CFG_MAX_BUTTON_ACTIONS) {
        LOG_WARN("BTN", "button '%s': %u actions exceed CFG_MAX_BUTTON_ACTIONS=%u — extras ignored",
                 widgetId, static_cast<unsigned>(total),
                 static_cast<unsigned>(CFG_MAX_BUTTON_ACTIONS));
    }
    for (JsonObjectConst a : actionsArr) {
        if (out->actionsCount >= CFG_MAX_BUTTON_ACTIONS)
            break;
        CfgButtonAction parsed{}; // value-init — parser zero-fills before switching on JSON
        parseButtonAction(a, &parsed);
        if (parsed.type == CfgButtonActionType::UNKNOWN) {
            LOG_WARN("BTN", "button '%s': unknown action type '%s' — skipped", widgetId,
                     static_cast<const char *>(a["type"] | ""));
            continue;
        }
        out->actions[out->actionsCount++] = parsed;
    }
}

// Synthesise a single NAV_PAGE action from the deprecated `targetPageId`
// field so legacy configs (pre-#673) keep navigating. Caller guarantees
// actionsCount==0 on entry.
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
    strlcpy(out->label, stateJson["label"] | "", CFG_MAX_NAME_LEN);
    const char *iconName = stateJson["iconName"] | "";
    out->hasIconName = iconName[0] != '\0';
    strlcpy(out->iconName, iconName, sizeof(out->iconName));

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
    if (total > CFG_MAX_CYCLE_STATES) {
        LOG_WARN(
            "BTN", "cycle button '%s': %u states exceed CFG_MAX_CYCLE_STATES=%u — extras ignored",
            widgetId, static_cast<unsigned>(total), static_cast<unsigned>(CFG_MAX_CYCLE_STATES));
    }
    for (JsonObjectConst stateJson : statesArr) {
        if (out->statesCount >= CFG_MAX_CYCLE_STATES)
            break;
        parseButtonState(stateJson, widgetId, out->statesCount, &out->states[out->statesCount]);
        out->statesCount++;
    }
}

void parseButtonWidget(JsonObjectConst cfg, const char *widgetId, CfgButtonParams *out) {
    strlcpy(out->label, cfg["label"] | "", CFG_MAX_NAME_LEN);
    strlcpy(out->iconPath, cfg["iconPath"] | "", CFG_MAX_PATH_LEN);
    strlcpy(out->iconName, cfg["iconName"] | "", sizeof(out->iconName));
    out->isToggle = cfg["isToggle"] | false;
    out->showIcon = cfg["showIcon"] | true;
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
    out->autoStart = cfg["autoStart"] | false;
    out->formatMsec = strcmp(cfg["format"] | "mm:ss", "ss.mmm") == 0;
}

void parseImageWidget(JsonObjectConst cfg, CfgImageParams *out) {
    strlcpy(out->imagePath, cfg["imagePath"] | "", CFG_MAX_PATH_LEN);
}

// Gear indicator reuses the label params for prefix/suffix. Kept separate
// because decimalPlaces is forced to 0 (the firmware always renders a single
// character) and alertThreshold isn't part of the schema.
void parseGearWidget(JsonObjectConst cfg, CfgLabelParams *out) {
    out->decimalPlaces = 0;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
}

} // namespace

void parseWidget(JsonObjectConst src, CfgWidget *w) {
    strlcpy(w->id, src["id"] | "", CFG_MAX_ID_LEN);
    strlcpy(w->signalId, src["signal"] | "", CFG_MAX_SIGNAL_LEN);
    w->type = parseWidgetType(src["type"] | "");

    parseWidgetLayout(src["layout"], &w->layout);
    parseWidgetStyle(src["style"], &w->style);

    JsonObjectConst cfg = src["config"];
    switch (w->type) {
        case WidgetType::GAUGE:
            parseGaugeWidget(cfg, w);
            break;
        case WidgetType::LABEL:
            parseLabelParams(cfg, &w->label, readAlertThreshold(cfg));
            break;
        case WidgetType::WARNING:
            parseWarningWidget(cfg, &w->warning);
            break;
        case WidgetType::BUTTON:
            parseButtonWidget(cfg, w->id, &w->button);
            break;
        case WidgetType::TIMER:
            parseTimerWidget(cfg, &w->timer);
            break;
        case WidgetType::IMAGE:
            parseImageWidget(cfg, &w->image);
            break;
        case WidgetType::GEAR_IND:
            parseGearWidget(cfg, &w->label);
            break;
        case WidgetType::UNKNOWN:
        default:
            break;
    }
}

} // namespace ConfigLoaderInternal
