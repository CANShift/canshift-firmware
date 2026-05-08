// config_loader.cpp — Config JSON parsing implementation

#include "config_loader.h"
#include "board_config.h"
#include "hal/storage/storage_driver.h"
#include "diag/logger.h"
#include "diag/error_store.h"

#include <ArduinoJson.h>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal storage for parsed configs
// ---------------------------------------------------------------------------

static CfgDashboard s_dashboard = {};
static CfgSignalConfig s_signals = {};
static CfgDeviceConfig s_device = {};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Suffix for the boot-time fallback copy written by atomic saves.
static constexpr const char *kBakSuffix = ".bak";
// CFG_MAX_PATH_LEN (48) + ".bak" + null terminator.
static constexpr size_t kBakPathLen = CFG_MAX_PATH_LEN + 5;

namespace {

bool buildBakPath(char *out, size_t outLen, const char *base) {
    if (!out || !base || outLen == 0)
        return false;
    const size_t baseLen = strlen(base);
    const size_t suffixLen = strlen(kBakSuffix);
    if (baseLen + suffixLen + 1 > outLen)
        return false;
    memcpy(out, base, baseLen);
    memcpy(out + baseLen, kBakSuffix, suffixLen);
    out[baseLen + suffixLen] = '\0';
    return true;
}

// Read + parse JSON from `path`, falling back to `<path>.bak` on missing or
// corrupt primary. On successful .bak recovery the .bak is renamed back to
// `path` so the next save re-establishes the .bak rotation cycle.
// Returns true on success (doc populated), false otherwise.
bool readAndParseWithBak(const char *path, JsonDocument &doc) {
    size_t jsonSize = 0;
    char *json = StorageDriver::readFile(path, &jsonSize);
    if (json) {
        DeserializationError err = deserializeJson(doc, json, jsonSize);
        free(json);
        if (!err)
            return true;
        LOG_WARN("CFG", "%s parse error: %s — falling back to .bak", path, err.c_str());
    } else {
        LOG_WARN("CFG", "%s missing — falling back to .bak", path);
    }

    char bakPath[kBakPathLen];
    if (!buildBakPath(bakPath, sizeof(bakPath), path))
        return false;
    if (!StorageDriver::fileExists(bakPath))
        return false;

    size_t bakSize = 0;
    char *bakJson = StorageDriver::readFile(bakPath, &bakSize);
    if (!bakJson)
        return false;

    doc.clear();
    DeserializationError bakErr = deserializeJson(doc, bakJson, bakSize);
    free(bakJson);
    if (bakErr) {
        LOG_ERROR("CFG", "%s also failed to parse: %s", bakPath, bakErr.c_str());
        return false;
    }

    LOG_WARN("CFG", "%s recovered from .bak", path);
    // Promote the .bak back into place so the next save creates a fresh .bak.
    if (!StorageDriver::renameFile(bakPath, path)) {
        LOG_WARN("CFG", "Could not rename %s back to %s — non-fatal", bakPath, path);
    }
    return true;
}

void parseColor(const char *hex, CfgColor *out) {
    if (!hex || hex[0] != '#') {
        out->rgb = 0x000000;
        return;
    }
    out->rgb = static_cast<uint32_t>(strtoul(hex + 1, nullptr, 16));
}

TopBarItemKind parseTopBarItemKind(const char *str) {
    if (!str)
        return TopBarItemKind::UNKNOWN;
    if (strcmp(str, "statusDot") == 0)
        return TopBarItemKind::STATUS_DOT;
    if (strcmp(str, "label") == 0)
        return TopBarItemKind::LABEL;
    if (strcmp(str, "separator") == 0)
        return TopBarItemKind::SEPARATOR;
    if (strcmp(str, "signal") == 0)
        return TopBarItemKind::SIGNAL;
    if (strcmp(str, "usbIcon") == 0)
        return TopBarItemKind::USB_ICON;
    if (strcmp(str, "themeToggle") == 0)
        return TopBarItemKind::THEME_TOGGLE;
    return TopBarItemKind::UNKNOWN;
}

TopBarItemPos parseTopBarItemPos(const char *str) {
    if (!str)
        return TopBarItemPos::LEFT;
    if (strcmp(str, "center") == 0)
        return TopBarItemPos::CENTER;
    if (strcmp(str, "right") == 0)
        return TopBarItemPos::RIGHT;
    return TopBarItemPos::LEFT;
}

// Decode an even-length, lowercase-or-uppercase hex string into a byte buffer.
// Returns true and writes `*outLen` on success. Returns false (without
// touching `out`) on any of:
//   - hex == nullptr
//   - odd length
//   - decoded length > maxLen
//   - any non-hex character in `hex`
// Empty input is allowed and yields *outLen == 0.
bool decodeHexBytes(const char *hex, uint8_t *out, uint8_t maxLen, uint8_t *outLen) {
    if (!hex || !out || !outLen)
        return false;
    const size_t hexLen = strlen(hex);
    if (hexLen % 2 != 0)
        return false;
    const size_t byteLen = hexLen / 2;
    if (byteLen > maxLen)
        return false;

    for (size_t i = 0; i < byteLen; ++i) {
        char buf[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        char *end = nullptr;
        const unsigned long v = strtoul(buf, &end, 16);
        if (end != buf + 2 || v > 0xFFu)
            return false;
        out[i] = static_cast<uint8_t>(v);
    }
    *outLen = static_cast<uint8_t>(byteLen);
    return true;
}

CfgButtonActionType parseButtonActionType(const char *category, const char *type) {
    if (!type)
        return CfgButtonActionType::UNKNOWN;
    if (strcmp(type, "navigate") == 0)
        return CfgButtonActionType::NAV_PAGE;
    if (strcmp(type, "map_switch") == 0)
        return CfgButtonActionType::MAP_SWITCH;
    if (strcmp(type, "can_raw") == 0)
        return CfgButtonActionType::CAN_RAW;
    (void)category;
    return CfgButtonActionType::UNKNOWN;
}

void parseButtonAction(JsonObjectConst src, CfgButtonAction *out) {
    const char *category = src["category"] | "";
    const char *type = src["type"] | "";
    out->type = parseButtonActionType(category, type);
    out->pageId[0] = '\0';
    out->mapIndex = 0;
    out->canFrameId = 0;
    out->canDataLen = 0;
    memset(out->canData, 0, sizeof(out->canData));

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
            break;
        }
        case CfgButtonActionType::UNKNOWN:
        default:
            break;
    }
}

CfgLabelPos parseLabelPos(const char *str) {
    if (!str)
        return CfgLabelPos::TOP_LEFT;
    if (strcmp(str, "top-left") == 0)
        return CfgLabelPos::TOP_LEFT;
    if (strcmp(str, "top-center") == 0)
        return CfgLabelPos::TOP_CENTER;
    if (strcmp(str, "top-right") == 0)
        return CfgLabelPos::TOP_RIGHT;
    if (strcmp(str, "bottom-left") == 0)
        return CfgLabelPos::BOTTOM_LEFT;
    if (strcmp(str, "bottom-center") == 0)
        return CfgLabelPos::BOTTOM_CENTER;
    if (strcmp(str, "bottom-right") == 0)
        return CfgLabelPos::BOTTOM_RIGHT;
    return CfgLabelPos::TOP_LEFT;
}

// Parse arcFillStyle (issue #175). Defaults to ZONES so legacy configs and
// unknown values keep the previous warn/danger sector tinting behaviour.
CfgArcFillStyle parseArcFillStyle(const char *str) {
    if (!str)
        return CfgArcFillStyle::ZONES;
    if (strcmp(str, "gradient") == 0)
        return CfgArcFillStyle::GRADIENT;
    return CfgArcFillStyle::ZONES;
}

// Extract the major component of a "major.minor.patch" version string.
// Returns -1 when the string is empty, missing, or not a parsable integer.
// Major-only comparison is intentional — minor/patch bumps are backward
// compatible (issue #203).
int parseMajorVersion(const char *version) {
    if (!version || version[0] == '\0')
        return -1;
    char *end = nullptr;
    const long major = strtol(version, &end, 10);
    if (end == version || major < 0 || major > INT_MAX)
        return -1;
    return static_cast<int>(major);
}

// Compare a config file's "version" string against CONFIG_SCHEMA_VERSION
// (firmware build, mirrored from canshift-core). Logs and pushes to
// ErrorStore on mismatch but does not abort — parse continues so the
// firmware can still surface whatever subset of the file it understands.
void checkSchemaVersion(const char *fileLabel, const char *fileVersion) {
    const int fileMajor = parseMajorVersion(fileVersion);
    const int firmwareMajor = parseMajorVersion(CONFIG_SCHEMA_VERSION);
    if (fileMajor < 0) {
        LOG_WARN("CFG", "%s: missing or invalid version field — proceeding", fileLabel);
        ErrorStore::push(ERROR_SRC_CONFIG, "VER_MISSING", fileLabel);
        return;
    }
    if (fileMajor != firmwareMajor) {
        LOG_ERROR("CFG", "%s schema version mismatch: file=%s firmware=%s", fileLabel,
                  fileVersion, CONFIG_SCHEMA_VERSION);
        char detail[52];
        snprintf(detail, sizeof(detail), "%s file=%s fw=%s", fileLabel, fileVersion,
                 CONFIG_SCHEMA_VERSION);
        ErrorStore::push(ERROR_SRC_CONFIG, "VER_MISMATCH", detail);
    }
}

WidgetType parseWidgetType(const char *str) {
    if (!str)
        return WidgetType::UNKNOWN;
    if (strcmp(str, "gauge") == 0)
        return WidgetType::GAUGE;
    if (strcmp(str, "label") == 0)
        return WidgetType::LABEL;
    if (strcmp(str, "warning") == 0)
        return WidgetType::WARNING;
    if (strcmp(str, "button") == 0)
        return WidgetType::BUTTON;
    if (strcmp(str, "timer") == 0)
        return WidgetType::TIMER;
    if (strcmp(str, "bar") == 0)
        return WidgetType::BAR;
    if (strcmp(str, "gear") == 0)
        return WidgetType::GEAR_IND;
    if (strcmp(str, "image") == 0)
        return WidgetType::IMAGE;
    return WidgetType::UNKNOWN;
}

void parseWidget(JsonObjectConst src, CfgWidget *w) {
    strlcpy(w->id, src["id"] | "", CFG_MAX_ID_LEN);
    strlcpy(w->signalId, src["signal"] | "", CFG_MAX_SIGNAL_LEN);
    w->type = parseWidgetType(src["type"] | "");

    JsonObjectConst layout = src["layout"];
    w->layout.x = layout["x"] | 0;
    w->layout.y = layout["y"] | 0;
    w->layout.w = layout["w"] | 80;
    w->layout.h = layout["h"] | 60;
    w->layout.zOrder = layout["zOrder"] | 0;

    JsonObjectConst style = src["style"];
    parseColor(style["primaryColor"] | "#FFFFFF", &w->style.primaryColor);
    parseColor(style["secondaryColor"] | "#333333", &w->style.secondaryColor);
    parseColor(style["warningColor"] | "#FF8800", &w->style.warningColor);
    parseColor(style["criticalColor"] | "#FF0000", &w->style.criticalColor);
    parseColor(style["textColor"] | "#FFFFFF", &w->style.textColor);
    w->style.fontSize = style["fontSize"] | 16;
    const char *borderHex = style["borderColor"] | nullptr;
    w->style.hasBorder = (borderHex != nullptr);
    if (w->style.hasBorder)
        parseColor(borderHex, &w->style.borderColor);

    JsonObjectConst cfg = src["config"];
    switch (w->type) {
        case WidgetType::GAUGE: {
            // dashboard.json uses type "gauge" with displayStyle "bar" or "numeric"
            // to encode sub-types — reclassify here so downstream renderers are simple.
            const char *displayStyle = cfg["displayStyle"] | "arc";
            // alertThreshold: optional, NaN sentinel = disabled (issue #133)
            const float alertThreshold =
                cfg["alertThreshold"].is<float>() ? cfg["alertThreshold"].as<float>() : NAN;
            if (strcmp(displayStyle, "bar") == 0) {
                w->type = WidgetType::BAR;
                w->bar.minValue = cfg["minValue"] | 0.0f;
                w->bar.maxValue = cfg["maxValue"] | 100.0f;
                w->bar.warningLevel = cfg["warningLevel"] | 80.0f;
                w->bar.dangerLevel = cfg["dangerLevel"] | 95.0f;
                w->bar.alertThreshold = alertThreshold;
                const char *orient = cfg["barOrientation"] | "horizontal";
                w->bar.isVertical = (strcmp(orient, "vertical") == 0);
                w->bar.decimalPlaces = cfg["decimalPlaces"] | 0;
                strlcpy(w->bar.prefix, cfg["prefix"] | "", sizeof(w->bar.prefix));
                strlcpy(w->bar.suffix, cfg["suffix"] | "", sizeof(w->bar.suffix));
                strlcpy(w->bar.label, cfg["label"] | "", sizeof(w->bar.label));
                w->bar.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
                strlcpy(w->bar.iconName, cfg["iconName"] | "", sizeof(w->bar.iconName));
            } else if (strcmp(displayStyle, "numeric") == 0) {
                // Large numeric readout — render as a label widget
                w->type = WidgetType::LABEL;
                w->label.decimalPlaces = cfg["decimalPlaces"] | 0;
                w->label.alertThreshold = alertThreshold;
                strlcpy(w->label.prefix, cfg["prefix"] | "", sizeof(w->label.prefix));
                strlcpy(w->label.suffix, cfg["suffix"] | "", sizeof(w->label.suffix));
                w->label.hideWhenInvalid = cfg["hideWhenInvalid"] | false;
                strlcpy(w->label.label, cfg["label"] | "", sizeof(w->label.label));
                w->label.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
            } else {
                // "arc" or unrecognised — arc gauge
                w->gauge.minValue = cfg["minValue"] | 0.0f;
                w->gauge.maxValue = cfg["maxValue"] | 100.0f;
                w->gauge.warningLevel = cfg["warningLevel"] | 80.0f;
                w->gauge.dangerLevel = cfg["dangerLevel"] | 95.0f;
                w->gauge.alertThreshold = alertThreshold;
                w->gauge.showNeedle = cfg["showNeedle"] | false;
                w->gauge.showArc = cfg["showArc"] | true;
                w->gauge.revFlash = cfg["revFlash"] | false;
                w->gauge.decimalPlaces = cfg["decimalPlaces"] | 0;
                strlcpy(w->gauge.prefix, cfg["prefix"] | "", sizeof(w->gauge.prefix));
                strlcpy(w->gauge.suffix, cfg["suffix"] | "", sizeof(w->gauge.suffix));
                strlcpy(w->gauge.label, cfg["label"] | "", sizeof(w->gauge.label));
                w->gauge.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
                w->gauge.arcFillStyle = parseArcFillStyle(cfg["arcFillStyle"] | "zones");
            }
            break;
        }
        case WidgetType::LABEL:
            w->label.decimalPlaces = cfg["decimalPlaces"] | 0;
            w->label.alertThreshold =
                cfg["alertThreshold"].is<float>() ? cfg["alertThreshold"].as<float>() : NAN;
            strlcpy(w->label.prefix, cfg["prefix"] | "", sizeof(w->label.prefix));
            strlcpy(w->label.suffix, cfg["suffix"] | "", sizeof(w->label.suffix));
            w->label.hideWhenInvalid = cfg["hideWhenInvalid"] | false;
            strlcpy(w->label.label, cfg["label"] | "", sizeof(w->label.label));
            w->label.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
            break;
        case WidgetType::WARNING:
            w->warning.invertLogic = cfg["invertLogic"] | false;
            w->warning.threshold = cfg["threshold"] | 0.5f;
            strlcpy(w->warning.iconName, cfg["iconName"] | "", sizeof(w->warning.iconName));
            strlcpy(w->warning.label, cfg["label"] | "", sizeof(w->warning.label));
            w->warning.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
            break;
        case WidgetType::BAR:
            // Direct "type": "bar" — BarWidgetConfig schema (always horizontal)
            w->bar.minValue = cfg["minValue"] | 0.0f;
            w->bar.maxValue = cfg["maxValue"] | 100.0f;
            w->bar.warningLevel = cfg["warningLevel"] | NAN;
            w->bar.dangerLevel = cfg["dangerLevel"] | NAN;
            w->bar.alertThreshold =
                cfg["alertThreshold"].is<float>() ? cfg["alertThreshold"].as<float>() : NAN;
            w->bar.isVertical = false;
            w->bar.decimalPlaces = cfg["decimalPlaces"] | 0;
            strlcpy(w->bar.prefix, cfg["prefix"] | "", sizeof(w->bar.prefix));
            strlcpy(w->bar.suffix, cfg["suffix"] | "", sizeof(w->bar.suffix));
            strlcpy(w->bar.label, cfg["label"] | "", sizeof(w->bar.label));
            w->bar.labelPosition = parseLabelPos(cfg["labelPosition"] | "bottom-center");
            strlcpy(w->bar.iconName, cfg["iconName"] | "", sizeof(w->bar.iconName));
            break;
        case WidgetType::BUTTON: {
            strlcpy(w->button.label, cfg["label"] | "", CFG_MAX_NAME_LEN);
            strlcpy(w->button.iconPath, cfg["iconPath"] | "", CFG_MAX_PATH_LEN);
            strlcpy(w->button.iconName, cfg["iconName"] | "", sizeof(w->button.iconName));
            w->button.isToggle = cfg["isToggle"] | false;
            w->button.showIcon = cfg["showIcon"] | true;
            w->button.showLabel = cfg["showLabel"] | true;
            JsonObjectConst colors = cfg["colors"];
            w->button.hasColors = !colors.isNull();
            if (w->button.hasColors) {
                parseColor(colors["normal"] | "#FFFFFF", &w->button.colorNormal);
                parseColor(colors["active"] | "#FFFFFF", &w->button.colorActive);
            } else {
                w->button.colorNormal.rgb = 0x000000;
                w->button.colorActive.rgb = 0x000000;
            }

            // Parse actions[]; fall back to legacy targetPageId when absent/empty.
            w->button.actionsCount = 0;
            JsonArrayConst actionsArr = cfg["actions"];
            if (!actionsArr.isNull()) {
                const size_t total = actionsArr.size();
                if (total > CFG_MAX_BUTTON_ACTIONS) {
                    LOG_WARN(
                        "BTN",
                        "button '%s': %u actions exceed CFG_MAX_BUTTON_ACTIONS=%u — extras ignored",
                        w->id, static_cast<unsigned>(total),
                        static_cast<unsigned>(CFG_MAX_BUTTON_ACTIONS));
                }
                for (JsonObjectConst a : actionsArr) {
                    if (w->button.actionsCount >= CFG_MAX_BUTTON_ACTIONS)
                        break;
                    CfgButtonAction parsed;
                    parseButtonAction(a, &parsed);
                    if (parsed.type == CfgButtonActionType::UNKNOWN) {
                        const char *type = a["type"] | "";
                        LOG_WARN("BTN", "button '%s': unknown action type '%s' — skipped", w->id,
                                 type);
                        continue;
                    }
                    w->button.actions[w->button.actionsCount++] = parsed;
                }
            }
            if (w->button.actionsCount == 0) {
                const char *legacy = cfg["targetPageId"] | "";
                if (legacy[0] != '\0') {
                    CfgButtonAction &a = w->button.actions[0];
                    a.type = CfgButtonActionType::NAV_PAGE;
                    strlcpy(a.pageId, legacy, sizeof(a.pageId));
                    a.mapIndex = 0;
                    a.canFrameId = 0;
                    a.canDataLen = 0;
                    memset(a.canData, 0, sizeof(a.canData));
                    w->button.actionsCount = 1;
                }
            }
            break;
        }
        case WidgetType::TIMER:
            w->timer.autoStart = cfg["autoStart"] | false;
            w->timer.formatMsec = strcmp(cfg["format"] | "mm:ss", "ss.mmm") == 0;
            strlcpy(w->timer.label, cfg["label"] | "", sizeof(w->timer.label));
            w->timer.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
            break;
        case WidgetType::IMAGE:
            strlcpy(w->image.imagePath, cfg["imagePath"] | "", CFG_MAX_PATH_LEN);
            strlcpy(w->image.label, cfg["label"] | "", sizeof(w->image.label));
            w->image.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
            break;
        case WidgetType::GEAR_IND:
            // Gear indicator reuses label params for prefix/suffix/hideWhenInvalid
            w->label.decimalPlaces = 0;
            strlcpy(w->label.prefix, cfg["prefix"] | "", sizeof(w->label.prefix));
            strlcpy(w->label.suffix, cfg["suffix"] | "", sizeof(w->label.suffix));
            w->label.hideWhenInvalid = cfg["hideWhenInvalid"] | false;
            strlcpy(w->label.label, cfg["label"] | "", sizeof(w->label.label));
            w->label.labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
            break;
        default:
            break;
    }
}

bool loadDashboard() {
    JsonDocument doc; // ArduinoJson v7 — dynamic, no capacity() needed
    if (!readAndParseWithBak(CONFIG_PATH_DASHBOARD, doc)) {
        LOG_ERROR("CFG", "dashboard.json unreadable (primary + .bak)");
        ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "dashboard.json unreadable");
        return false;
    }

    strlcpy(s_dashboard.version, doc["version"] | "", sizeof(s_dashboard.version));
    checkSchemaVersion("dashboard.json", s_dashboard.version);
    strlcpy(s_dashboard.name, doc["name"] | "", sizeof(s_dashboard.name));
    strlcpy(s_dashboard.defaultPageId, doc["defaultPageId"] | "",
            sizeof(s_dashboard.defaultPageId));
    s_dashboard.revLimitRpm = doc["revLimitRpm"] | 7200.0f;

    JsonObjectConst topBar = doc["topBar"];
    s_dashboard.topBar.height = topBar["height"] | 24;
    parseColor(topBar["bgColor"] | "#111111", &s_dashboard.topBar.bgColor);
    parseColor(topBar["textColor"] | "#AAAAAA", &s_dashboard.topBar.textColor);

    JsonArrayConst topBarLayout = topBar["layout"];
    s_dashboard.topBar.hasLayout = !topBarLayout.isNull();
    s_dashboard.topBar.itemCount = 0;
    if (s_dashboard.topBar.hasLayout) {
        const size_t total = topBarLayout.size();
        if (total > CFG_MAX_TOPBAR_ITEMS) {
            LOG_WARN("CFG",
                     "topBar.layout: %u items exceed CFG_MAX_TOPBAR_ITEMS=%u — extras ignored",
                     static_cast<unsigned>(total), CFG_MAX_TOPBAR_ITEMS);
        }
        for (JsonObjectConst item : topBarLayout) {
            if (s_dashboard.topBar.itemCount >= CFG_MAX_TOPBAR_ITEMS)
                break;
            CfgTopBarItem &out = s_dashboard.topBar.items[s_dashboard.topBar.itemCount];
            out.kind = parseTopBarItemKind(item["type"] | "");
            out.position = parseTopBarItemPos(item["position"] | "left");
            strlcpy(out.signalId, item["signal"] | "", CFG_MAX_SIGNAL_LEN);
            strlcpy(out.text, item["text"] | "", sizeof(out.text));
            strlcpy(out.format, item["format"] | "", sizeof(out.format));
            if (out.kind == TopBarItemKind::UNKNOWN) {
                LOG_WARN("CFG", "topBar.layout[%u]: unknown type — item dropped",
                         static_cast<unsigned>(s_dashboard.topBar.itemCount));
                continue; // don't increment count — leave the slot reusable
            }
            ++s_dashboard.topBar.itemCount;
        }
    }

    // Optional day theme (hasDayTheme = false when key absent)
    JsonObjectConst dayThemeJson = doc["dayTheme"];
    s_dashboard.hasDayTheme = !dayThemeJson.isNull();
    if (s_dashboard.hasDayTheme) {
        parseColor(dayThemeJson["bgColor"] | "#DDDDDD", &s_dashboard.dayTheme.bgColor);
        JsonObjectConst dp = dayThemeJson["palette"];
        parseColor(dp["surface"] | "#F0F0F0", &s_dashboard.dayTheme.palette.surface);
        parseColor(dp["primary"] | "#CC0000", &s_dashboard.dayTheme.palette.primary);
        parseColor(dp["accent"] | "#E06000", &s_dashboard.dayTheme.palette.accent);
        parseColor(dp["text"] | "#000000", &s_dashboard.dayTheme.palette.text);
        parseColor(dp["textDim"] | "#444444", &s_dashboard.dayTheme.palette.textDim);
        parseColor(dp["warning"] | "#CC6600", &s_dashboard.dayTheme.palette.warning);
        parseColor(dp["danger"] | "#CC0000", &s_dashboard.dayTheme.palette.danger);
        parseColor(dp["success"] | "#006622", &s_dashboard.dayTheme.palette.success);
    }

    JsonArrayConst pages = doc["pages"];
    s_dashboard.pageCount = 0;
    const size_t totalPages = pages.size();
    if (totalPages > CONFIG_MAX_PAGES) {
        LOG_WARN("CFG", "dashboard.json: %u pages exceed CONFIG_MAX_PAGES=%u — extras ignored",
                 static_cast<unsigned>(totalPages), CONFIG_MAX_PAGES);
    }

    for (JsonObjectConst page : pages) {
        if (s_dashboard.pageCount >= CONFIG_MAX_PAGES)
            break;

        CfgPage &p = s_dashboard.pages[s_dashboard.pageCount++];
        strlcpy(p.id, page["id"] | "", CFG_MAX_ID_LEN);
        strlcpy(p.bgImagePath, page["backgroundImage"] | "", CFG_MAX_PATH_LEN);
        parseColor(page["backgroundColor"] | "#1A1A1A", &p.bgColor);

        JsonObjectConst palette = page["palette"];
        parseColor(palette["surface"] | "#1E1E1E", &p.palette.surface);
        parseColor(palette["primary"] | "#FF4444", &p.palette.primary);
        parseColor(palette["accent"] | "#FF8800", &p.palette.accent);
        parseColor(palette["text"] | "#FFFFFF", &p.palette.text);
        parseColor(palette["textDim"] | "#888888", &p.palette.textDim);
        parseColor(palette["warning"] | "#FF8800", &p.palette.warning);
        parseColor(palette["danger"] | "#FF4444", &p.palette.danger);
        parseColor(palette["success"] | "#00CC44", &p.palette.success);

        p.showTopBar = page["showTopBar"] | true;
        p.visible = page["visible"] | true;
        p.widgetCount = 0;

        JsonArrayConst widgets = page["widgets"];
        const size_t totalWidgets = widgets.size();
        if (totalWidgets > CONFIG_MAX_WIDGETS_PER_PAGE) {
            LOG_WARN("CFG",
                     "page '%s': %u widgets exceed CONFIG_MAX_WIDGETS_PER_PAGE=%u — extras ignored",
                     p.id, static_cast<unsigned>(totalWidgets), CONFIG_MAX_WIDGETS_PER_PAGE);
        }
        for (JsonObjectConst w : widgets) {
            if (p.widgetCount >= CONFIG_MAX_WIDGETS_PER_PAGE)
                break;
            parseWidget(w, &p.widgets[p.widgetCount++]);
        }
    }

    s_dashboard.loaded = true;
    LOG_INFO("CFG", "dashboard.json loaded: %d pages", s_dashboard.pageCount);
    return true;
}

bool loadSignals() {
    JsonDocument doc; // ArduinoJson v7 — dynamic
    if (!readAndParseWithBak(CONFIG_PATH_SIGNALS, doc)) {
        LOG_ERROR("CFG", "signals.json unreadable (primary + .bak)");
        ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "signals.json unreadable");
        return false;
    }

    strlcpy(s_signals.version, doc["version"] | "", sizeof(s_signals.version));
    checkSchemaVersion("signals.json", s_signals.version);
    strlcpy(s_signals.protocol, doc["protocol"] | "", sizeof(s_signals.protocol));
    s_signals.canSpeedKbps = doc["canSpeedKbps"] | 500;
    s_signals.signalCount = 0;

    JsonArrayConst signals = doc["signals"];
    for (JsonObjectConst sig : signals) {
        if (s_signals.signalCount >= CONFIG_MAX_SIGNALS)
            break;

        CfgSignalDef &s = s_signals.signals[s_signals.signalCount++];
        strlcpy(s.name, sig["name"] | "", CFG_MAX_SIGNAL_LEN);
        s.canFrameId = strtoul(sig["canFrameId"] | "0x0", nullptr, 16);
        s.startByte = sig["startByte"] | 0;
        s.byteLength = sig["byteLength"] | 1;
        s.bigEndian = sig["bigEndian"] | true;
        s.isSigned = sig["signed"] | false;
        s.scale = sig["scale"] | 1.0f;
        s.offset = sig["offset"] | 0.0f;
        strlcpy(s.unit, sig["unit"] | "", sizeof(s.unit));
        s.minValue = sig["min"] | 0.0f;
        s.maxValue = sig["max"] | 100.0f;
        {
            JsonVariantConst wv = sig["warningLevel"];
            s.warningLevel = wv.isNull() ? NAN : wv.as<float>();
            JsonVariantConst dv = sig["dangerLevel"];
            s.dangerLevel = dv.isNull() ? NAN : dv.as<float>();
            JsonVariantConst hwv = sig["highWarningLevel"];
            s.highWarningLevel = hwv.isNull() ? NAN : hwv.as<float>();
            JsonVariantConst hdv = sig["highDangerLevel"];
            s.highDangerLevel = hdv.isNull() ? NAN : hdv.as<float>();
        }
        s.timeoutMs = sig["timeoutMs"] | SIGNAL_DEFAULT_TIMEOUT_MS;
        const char *bitMaskStr = sig["bitMask"] | nullptr;
        s.bitMask = bitMaskStr ? static_cast<uint8_t>(strtoul(bitMaskStr, nullptr, 16)) : 0;

        // ----- Validate decoder-critical fields (issues #197 / #198) -----
        // CAN classic frames are 8 bytes; byteLength must be 1, 2, or 4 to
        // produce a well-defined sign-extend and a bounded read.
        static constexpr uint8_t kCanFrameMaxBytes = 8;
        const bool byteLenValid = (s.byteLength == 1 || s.byteLength == 2 || s.byteLength == 4);
        const bool startInRange = (s.startByte < kCanFrameMaxBytes);
        const bool fitsInFrame =
            (static_cast<uint16_t>(s.startByte) + static_cast<uint16_t>(s.byteLength)
             <= kCanFrameMaxBytes);
        if (!byteLenValid || !startInRange || !fitsInFrame) {
            LOG_WARN("CFG",
                     "signals.json: dropping '%s' (startByte=%u byteLength=%u) — out of range",
                     s.name, s.startByte, s.byteLength);
            --s_signals.signalCount;
            continue;
        }
    }

    s_signals.loaded = true;
    LOG_INFO("CFG", "signals.json loaded: %d signals", s_signals.signalCount);
    return true;
}

bool loadDevice() {
    size_t jsonSize = 0;
    char *json = StorageDriver::readFile(CONFIG_PATH_DEVICE, &jsonSize);
    if (!json) {
        LOG_INFO("CFG", "device.json not found — using board_config.h defaults");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, jsonSize);
    free(json);

    if (err) {
        LOG_WARN("CFG", "device.json parse error: %s — using defaults", err.c_str());
        return false;
    }

    s_device.canSpeedKbps = doc["can_speed_kbps"] | 0;
    s_device.twaiTxPin = doc["twai_tx_pin"] | -1;
    s_device.twaiRxPin = doc["twai_rx_pin"] | -1;
    s_device.loaded = true;
    LOG_INFO("CFG", "device.json loaded: CAN=%ukbps TX=GPIO%d RX=GPIO%d", s_device.canSpeedKbps,
             s_device.twaiTxPin, s_device.twaiRxPin);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ConfigLoader::LoadResult ConfigLoader::loadAll() {
    LoadResult r;
    r.dashboardOk = loadDashboard();
    r.signalsOk = loadSignals();
    r.deviceOk = loadDevice();
    return r;
}

const CfgDashboard &ConfigLoader::getDashboardConfig() {
    return s_dashboard;
}
const CfgSignalConfig &ConfigLoader::getSignalConfig() {
    return s_signals;
}
const CfgDeviceConfig &ConfigLoader::getDeviceConfig() {
    return s_device;
}
bool ConfigLoader::reloadAll() {
    LoadResult r = loadAll();
    LOG_INFO("CFG", "Config reloaded: dashboard=%d signals=%d", r.dashboardOk, r.signalsOk);
    return r.dashboardOk; // Dashboard is mandatory
}
