// config_loader.cpp — Config JSON parsing implementation

#include "config_loader.h"
#include "board_config.h"
#include "hal/storage/storage_driver.h"
#include "config/json_reader.h"
#include "diag/logger.h"
#include "diag/error_store.h"

#include <ArduinoJson.h>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string.h>

#ifdef ARDUINO
    #include <esp_heap_caps.h>
#endif

namespace {
// One-line heap snapshot tag. Logged before each config parse so an OOM in
// the field has a deterministic "last good free-block size" to look back at
// (issue #576). Cheap: heap_caps_get_largest_free_block walks the free list
// once. Compiled out on the host build.
void logLargestFreeBlock(const char *where) {
#ifdef ARDUINO
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    LOG_INFO("CFG", "heap.largest_free=%u before %s", static_cast<unsigned>(largest), where);
#else
    (void)where;
#endif
}
} // namespace

// ---------------------------------------------------------------------------
// Internal storage for parsed configs
// ---------------------------------------------------------------------------

static CfgDashboard s_dashboard = {};
static CfgSignalConfig s_signals = {};
static CfgDeviceConfig s_device = {};
static CfgInputBindings s_inputs = {};

// Transactional reload snapshot (issue #458, audit F-HI-3 / umbrella #1014).
// Previously these snapshots were heap-allocated on each load* entry and freed
// on exit (~22 KB + ~5 KB of malloc/free churn per reload), which fragmented
// the LVGL pool when fonts/SPIFFS loaded right after boot (related to #895 /
// #976). Now BSS-resident so load*() never touches the heap.
//
// loadDashboard() and loadSignals() run sequentially on the same task
// (ConfigLoader::loadAll drives them in order) and each completes before the
// next begins, so a single shared buffer sized to the larger of the two types
// is enough. Two separate buffers cost ~5 KB extra BSS and pushed the
// production crowpanel_28 image past the DRAM ceiling (umbrella #1014).
// The static_assert pins the invariant so a future struct growth on the
// other type can't silently underflow this buffer.
static_assert(sizeof(CfgDashboard) >= sizeof(CfgSignalConfig),
              "rollback snapshot buffer must fit CfgDashboard (the larger of the two)");
alignas(CfgDashboard) static uint8_t s_rollback_snapshot[sizeof(CfgDashboard)];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Suffix for the boot-time fallback copy written by atomic saves.
static constexpr const char *kBakSuffix = ".bak";
// CFG_MAX_PATH_LEN + ".bak" + null terminator.
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
//
// The implementation streams the SPIFFS File straight into the JsonDocument
// (no contiguous file-sized malloc). This eliminates the ~21 KB readFile()
// allocation that caused boot OOM with the LV_MEM_SIZE bump in #555
// (issue #576).
bool readAndParseWithBak(const char *path, JsonDocument &doc) {
    logLargestFreeBlock(path);

    if (StorageDriver::fileExists(path)) {
        DeserializationError err = StorageDriver::parseJsonFile(path, doc);
        if (!err)
            return true;
        if (err == DeserializationError::EmptyInput) {
            LOG_WARN("CFG", "%s could not be opened — falling back to .bak", path);
        } else {
            LOG_WARN("CFG", "%s parse error: %s — falling back to .bak", path, err.c_str());
        }
    } else {
        LOG_WARN("CFG", "%s missing — falling back to .bak", path);
    }

    char bakPath[kBakPathLen];
    if (!buildBakPath(bakPath, sizeof(bakPath), path))
        return false;
    if (!StorageDriver::fileExists(bakPath))
        return false;

    doc.clear();
    DeserializationError bakErr = StorageDriver::parseJsonFile(bakPath, doc);
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
    if (strcmp(str, "bleIcon") == 0)
        return TopBarItemKind::BLE_ICON;
    if (strcmp(str, "themeToggle") == 0)
        return TopBarItemKind::THEME_TOGGLE;
    if (strcmp(str, "modeFlag") == 0)
        return TopBarItemKind::MODE_FLAG;
    if (strcmp(str, "trackBadge") == 0)
        return TopBarItemKind::TRACK_BADGE;
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
        char buf[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
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
    if (strcmp(type, "cruise_control") == 0)
        return CfgButtonActionType::CRUISE_CONTROL;
    (void)category;
    return CfgButtonActionType::UNKNOWN;
}

CfgCruiseOp parseCruiseOp(const char *op) {
    if (!op)
        return CfgCruiseOp::UNKNOWN;
    if (strcmp(op, "on") == 0)
        return CfgCruiseOp::ON;
    if (strcmp(op, "off") == 0)
        return CfgCruiseOp::OFF;
    if (strcmp(op, "toggle") == 0)
        return CfgCruiseOp::TOGGLE;
    if (strcmp(op, "set") == 0)
        return CfgCruiseOp::SET;
    if (strcmp(op, "resume") == 0)
        return CfgCruiseOp::RESUME;
    if (strcmp(op, "increment") == 0)
        return CfgCruiseOp::INCREMENT;
    if (strcmp(op, "decrement") == 0)
        return CfgCruiseOp::DECREMENT;
    return CfgCruiseOp::UNKNOWN;
}

// Standard 11-bit CAN identifier max — anything above this requires the
// extended (29-bit) frame format (issue #319).
constexpr uint32_t CAN_STANDARD_ID_MAX = 0x7FFu;
// 29-bit extended CAN identifier max — covers all valid TWAI extended IDs.
constexpr uint32_t CAN_EXTENDED_ID_MAX = 0x1FFFFFFFu;

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
            const bool needsExtended = out->canFrameId > CAN_STANDARD_ID_MAX;
            out->canExtended = extendedFlag || needsExtended;
            if (out->canFrameId > CAN_EXTENDED_ID_MAX) {
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

// Parse a single hex color ("#RRGGBB"). Returns the unsigned 0x00RRGGBB
// payload, or 0 on malformed input — callers tolerate fallback to black.
uint32_t parseHexColorValue(const char *hex) {
    if (!hex || hex[0] != '#')
        return 0u;
    return static_cast<uint32_t>(strtoul(hex + 1, nullptr, 16));
}

// Decode signals.json `colorRamp` into the firmware-side struct (issue #430).
// Tolerates missing fields: out->count=0 leaves the signal on the default
// sensor lookup. Drops stops beyond CFG_MAX_RAMP_STOPS with a warning.
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
        LOG_ERROR("CFG", "%s schema version mismatch: file=%s firmware=%s", fileLabel, fileVersion,
                  CONFIG_SCHEMA_VERSION);
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
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
    out->arcFillStyle = parseArcFillStyle(cfg["arcFillStyle"] | "zones");
    strlcpy(out->iconName, cfg["iconName"] | "", sizeof(out->iconName));
}

// Bar parameters reached via the legacy `gauge` widget with displayStyle=bar.
// Behaviour identical to the old monolith — kept separate from the direct
// "type":"bar" parser because the default `dangerLevel` differs (hardcoded 95
// vs. NaN for the standalone BarWidgetConfig).
void parseGaugeBarParams(JsonObjectConst cfg, CfgBarParams *out, float alertThreshold) {
    out->minValue = cfg["minValue"] | 0.0f;
    out->maxValue = cfg["maxValue"] | 100.0f;
    out->dangerLevel = cfg["dangerLevel"] | 95.0f;
    out->alertThreshold = alertThreshold;
    const char *orient = cfg["barOrientation"] | "horizontal";
    out->isVertical = (strcmp(orient, "vertical") == 0);
    out->decimalPlaces = cfg["decimalPlaces"] | 0;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
    strlcpy(out->iconName, cfg["iconName"] | "", sizeof(out->iconName));
}

void parseLabelParams(JsonObjectConst cfg, CfgLabelParams *out, float alertThreshold) {
    out->decimalPlaces = cfg["decimalPlaces"] | 0;
    out->alertThreshold = alertThreshold;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    out->hideWhenInvalid = cfg["hideWhenInvalid"] | false;
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
}

// Gauge widgets in dashboard.json use displayStyle "bar" / "numeric" / "arc"
// to encode sub-types — reclassify the outgoing WidgetType here so downstream
// renderers can stay simple and dispatch on a single enum.
void parseGaugeWidget(JsonObjectConst cfg, CfgWidget *w) {
    const char *displayStyle = cfg["displayStyle"] | "arc";
    const float alertThreshold = readAlertThreshold(cfg);
    if (strcmp(displayStyle, "bar") == 0) {
        w->type = WidgetType::BAR;
        parseGaugeBarParams(cfg, &w->bar, alertThreshold);
    } else if (strcmp(displayStyle, "numeric") == 0) {
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
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
}

// Direct "type":"bar" — BarWidgetConfig schema (always horizontal, dangerLevel
// defaults to NaN unlike the legacy gauge-bar reclassification path).
void parseBarWidget(JsonObjectConst cfg, CfgBarParams *out) {
    out->minValue = cfg["minValue"] | 0.0f;
    out->maxValue = cfg["maxValue"] | 100.0f;
    out->dangerLevel = cfg["dangerLevel"] | NAN;
    out->alertThreshold = readAlertThreshold(cfg);
    out->isVertical = false;
    out->decimalPlaces = cfg["decimalPlaces"] | 0;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "bottom-center");
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
        CfgButtonAction parsed;
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

void parseButtonWidget(JsonObjectConst cfg, const char *widgetId, CfgButtonParams *out) {
    strlcpy(out->label, cfg["label"] | "", CFG_MAX_NAME_LEN);
    strlcpy(out->iconPath, cfg["iconPath"] | "", CFG_MAX_PATH_LEN);
    strlcpy(out->iconName, cfg["iconName"] | "", sizeof(out->iconName));
    out->isToggle = cfg["isToggle"] | false;
    out->showIcon = cfg["showIcon"] | true;
    out->showLabel = cfg["showLabel"] | true;
    parseButtonColors(cfg, out);
    out->actionsCount = 0;
    parseButtonActionsArray(cfg["actions"], widgetId, out);
    if (out->actionsCount == 0)
        applyLegacyTargetPageId(cfg, out);
}

void parseTimerWidget(JsonObjectConst cfg, CfgTimerParams *out) {
    out->autoStart = cfg["autoStart"] | false;
    out->formatMsec = strcmp(cfg["format"] | "mm:ss", "ss.mmm") == 0;
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
}

void parseImageWidget(JsonObjectConst cfg, CfgImageParams *out) {
    strlcpy(out->imagePath, cfg["imagePath"] | "", CFG_MAX_PATH_LEN);
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
}

// Gear indicator reuses the label params for prefix/suffix/hideWhenInvalid.
// Kept separate because decimalPlaces is forced to 0 (the firmware always
// renders a single character) and alertThreshold isn't part of the schema.
void parseGearWidget(JsonObjectConst cfg, CfgLabelParams *out) {
    out->decimalPlaces = 0;
    strlcpy(out->prefix, cfg["prefix"] | "", sizeof(out->prefix));
    strlcpy(out->suffix, cfg["suffix"] | "", sizeof(out->suffix));
    out->hideWhenInvalid = cfg["hideWhenInvalid"] | false;
    strlcpy(out->label, cfg["label"] | "", sizeof(out->label));
    out->labelPosition = parseLabelPos(cfg["labelPosition"] | "top-left");
}

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
        case WidgetType::BAR:
            parseBarWidget(cfg, &w->bar);
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

bool loadDashboard() {
    // Snapshot the previous in-memory state into the BSS-resident buffer
    // before touching s_dashboard so we can roll back atomically on any parse
    // failure (issue #458, audit F-HI-3 / umbrella #1014). All subsequent
    // mutations write directly into s_dashboard; on failure the snapshot is
    // copied back, leaving observable state byte-identical to the pre-call
    // value. BSS storage cannot fail, so the previous "fall through without
    // rollback on alloc failure" branch is gone.
    memcpy(s_rollback_snapshot, &s_dashboard, sizeof(CfgDashboard));

    JsonDocument doc; // ArduinoJson v7 — dynamic, no capacity() needed
    if (!readAndParseWithBak(CONFIG_PATH_DASHBOARD, doc)) {
        LOG_ERROR("CFG", "dashboard.json unreadable (primary + .bak)");
        ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "dashboard.json unreadable");
        memcpy(&s_dashboard, s_rollback_snapshot, sizeof(CfgDashboard));
        return false;
    }

    strlcpy(s_dashboard.version, doc["version"] | "", sizeof(s_dashboard.version));
    checkSchemaVersion("dashboard.json", s_dashboard.version);
    strlcpy(s_dashboard.name, doc["name"] | "", sizeof(s_dashboard.name));
    strlcpy(s_dashboard.defaultPageId, doc["defaultPageId"] | "",
            sizeof(s_dashboard.defaultPageId));
    s_dashboard.revLimitRpm = doc["revLimitRpm"] | 7200.0f;

    JsonObjectConst topBar = doc["topBar"];
    s_dashboard.topBar.height = topBar["height"] | 30;
    parseColor(topBar["bgColor"] | "#111111", &s_dashboard.topBar.bgColor);
    parseColor(topBar["textColor"] | "#AAAAAA", &s_dashboard.topBar.textColor);

    JsonArrayConst topBarLayout = topBar["layout"];
    s_dashboard.topBar.itemCount = 0;
    if (!topBarLayout.isNull()) {
        const size_t total = topBarLayout.size();
        if (total > CFG_MAX_TOPBAR_ITEMS) {
            LOG_WARN("CFG",
                     "topBar.layout: %u items exceed CFG_MAX_TOPBAR_ITEMS=%u — extras ignored",
                     static_cast<unsigned>(total), CFG_MAX_TOPBAR_ITEMS);
        }
        for (JsonObjectConst item : topBarLayout) {
            if (s_dashboard.topBar.itemCount >= CFG_MAX_TOPBAR_ITEMS)
                break;
            CfgTopBarItem tmp{};
            tmp.kind = parseTopBarItemKind(item["type"] | "");
            tmp.position = parseTopBarItemPos(item["position"] | "left");
            strlcpy(tmp.signalId, item["signal"] | "", CFG_MAX_SIGNAL_LEN);
            strlcpy(tmp.text, item["text"] | "", sizeof(tmp.text));
            strlcpy(tmp.format, item["format"] | "", sizeof(tmp.format));
            if (tmp.kind == TopBarItemKind::UNKNOWN) {
                LOG_WARN("CFG", "topBar.layout[%u]: unknown type — item dropped",
                         static_cast<unsigned>(s_dashboard.topBar.itemCount));
                continue; // slot untouched — no stale data risk
            }
            s_dashboard.topBar.items[s_dashboard.topBar.itemCount++] = tmp;
        }
    } else {
        // No layout in config — mirror canshift-core DEFAULT_TOP_BAR_LAYOUT
        LOG_INFO("CFG", "topBar.layout missing — applying default layout");
        uint8_t &n = s_dashboard.topBar.itemCount;
        auto &items = s_dashboard.topBar.items;
        auto addItem = [&](TopBarItemKind kind, TopBarItemPos pos, const char *sig,
                           const char *txt) {
            if (n >= CFG_MAX_TOPBAR_ITEMS)
                return;
            CfgTopBarItem &out = items[n++];
            out.kind = kind;
            out.position = pos;
            strlcpy(out.signalId, sig, sizeof(out.signalId));
            strlcpy(out.text, txt, sizeof(out.text));
            out.format[0] = '\0';
        };
        addItem(TopBarItemKind::STATUS_DOT, TopBarItemPos::LEFT, "any", "");
        addItem(TopBarItemKind::LABEL, TopBarItemPos::LEFT, "", "CAN");
        addItem(TopBarItemKind::BLE_ICON, TopBarItemPos::RIGHT, "", "");
        addItem(TopBarItemKind::USB_ICON, TopBarItemPos::RIGHT, "", "");
        addItem(TopBarItemKind::SEPARATOR, TopBarItemPos::RIGHT, "", "");
        addItem(TopBarItemKind::THEME_TOGGLE, TopBarItemPos::RIGHT, "", "");
    }

    // Optional day theme (hasDayTheme = false when key absent)
    JsonObjectConst dayThemeJson = doc["dayTheme"];
    s_dashboard.hasDayTheme = !dayThemeJson.isNull();
    if (s_dashboard.hasDayTheme) {
        parseColor(dayThemeJson["bgColor"] | "#DDDDDD", &s_dashboard.dayTheme.bgColor);
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
    // Snapshot prior state into the BSS-resident buffer for rollback on parse
    // failure (issue #458, audit F-HI-3 / umbrella #1014) — same pattern as
    // loadDashboard(). Boot-time config load is serial and loadDashboard has
    // already returned, so the shared rollback buffer is free to reuse here.
    memcpy(s_rollback_snapshot, &s_signals, sizeof(CfgSignalConfig));

    JsonDocument doc; // ArduinoJson v7 — dynamic
    if (!readAndParseWithBak(CONFIG_PATH_SIGNALS, doc)) {
        LOG_ERROR("CFG", "signals.json unreadable (primary + .bak)");
        ErrorStore::push(ERROR_SRC_CONFIG, "READ_FAIL", "signals.json unreadable");
        memcpy(&s_signals, s_rollback_snapshot, sizeof(CfgSignalConfig));
        return false;
    }

    strlcpy(s_signals.version, doc["version"] | "", sizeof(s_signals.version));
    checkSchemaVersion("signals.json", s_signals.version);
    strlcpy(s_signals.protocol, doc["protocol"] | "", sizeof(s_signals.protocol));
    s_signals.canSpeedKbps = doc["canSpeedKbps"] | 500;
    s_signals.signalCount = 0;

    // Outbound frame overrides — issue #317. signals.json `out` block lets the
    // user override baked frame IDs (currently just map_switch). Missing keys
    // leave the field at zero so the dispatcher falls back to the compiled-in
    // default. `extended` is auto-set when the ID exceeds the 11-bit range.
    s_signals.out.mapSwitchFrameId = 0;
    s_signals.out.mapSwitchExtended = false;
    JsonObjectConst outObj = doc["out"];
    if (!outObj.isNull()) {
        JsonObjectConst mapSwitch = outObj["map_switch"];
        if (!mapSwitch.isNull()) {
            JsonVariantConst idv = mapSwitch["id"];
            uint32_t id = 0;
            if (idv.is<uint32_t>()) {
                id = idv.as<uint32_t>();
            } else {
                const char *idStr = idv.as<const char *>();
                if (idStr)
                    id = static_cast<uint32_t>(strtoul(idStr, nullptr, 0));
            }
            if (id > CAN_EXTENDED_ID_MAX) {
                LOG_WARN("CFG", "signals.json out.map_switch.id=0x%lX exceeds 29-bit max — ignored",
                         static_cast<unsigned long>(id));
                id = 0;
            }
            s_signals.out.mapSwitchFrameId = id;
            const bool extendedFlag = mapSwitch["extended"] | false;
            s_signals.out.mapSwitchExtended = extendedFlag || (id > CAN_STANDARD_ID_MAX);
        }
    }

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

        // Per-signal color ramp (issue #430). Optional — when absent, the
        // widget renderer falls back to a default lookup keyed on the name.
        parseColorRamp(sig["colorRamp"], s.name, &s.colorRamp);

        // ----- Validate decoder-critical fields (issues #197 / #198) -----
        // CAN classic frames are 8 bytes; byteLength must be 1, 2, or 4 to
        // produce a well-defined sign-extend and a bounded read.
        static constexpr uint8_t kCanFrameMaxBytes = 8;
        const bool byteLenValid = (s.byteLength == 1 || s.byteLength == 2 || s.byteLength == 4);
        const bool startInRange = (s.startByte < kCanFrameMaxBytes);
        const bool fitsInFrame =
            (static_cast<uint16_t>(s.startByte) + static_cast<uint16_t>(s.byteLength) <=
             kCanFrameMaxBytes);
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
    // device.json is small (CfgDeviceConfig is ~8 bytes); snapshot lives on
    // the stack. The rollback is defensive — today the only mutations live
    // at the tail of this function, but keeping the pattern uniform with
    // loadDashboard / loadSignals (issue #458) protects against future
    // refactors that move assignments earlier.
    const CfgDeviceConfig prev = s_device;

    if (!StorageDriver::fileExists(CONFIG_PATH_DEVICE)) {
        LOG_INFO("CFG", "device.json not found — using board_config.h defaults");
        s_device = prev;
        return false;
    }

    logLargestFreeBlock(CONFIG_PATH_DEVICE);

    JsonDocument doc;
    DeserializationError err = StorageDriver::parseJsonFile(CONFIG_PATH_DEVICE, doc);

    if (err) {
        LOG_WARN("CFG", "device.json parse error: %s — using defaults", err.c_str());
        s_device = prev;
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

CfgInputActive parseInputActive(const char *str) {
    if (str && strcmp(str, "high") == 0)
        return CfgInputActive::ACTIVE_HIGH;
    return CfgInputActive::ACTIVE_LOW;
}

CfgInputPressKind parseInputPressKind(const char *str) {
    if (!str)
        return CfgInputPressKind::SHORT;
    if (strcmp(str, "long") == 0)
        return CfgInputPressKind::LONG;
    if (strcmp(str, "double") == 0)
        return CfgInputPressKind::DOUBLE;
    return CfgInputPressKind::SHORT;
}

// Reject pins already claimed by the active TWAI config. Display/touch pins
// are baked into board_config.h and a fuller cross-check belongs in a
// follow-up alongside hardware_profile.h.
bool isPinConflict(int8_t pin) {
    const CfgDeviceConfig &dev = s_device;
    if (!dev.loaded)
        return false;
    return pin == dev.twaiTxPin || pin == dev.twaiRxPin;
}

bool loadInputBindings() {
    s_inputs.count = 0;
    s_inputs.loaded = false;
    memset(s_inputs.bindings, 0, sizeof(s_inputs.bindings));

    if (!StorageDriver::fileExists(CONFIG_PATH_INPUTS)) {
        LOG_INFO("CFG", "input_bindings.json not found — no physical buttons configured");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = StorageDriver::parseJsonFile(CONFIG_PATH_INPUTS, doc);
    if (err) {
        LOG_WARN("CFG", "input_bindings.json parse error: %s — ignoring file", err.c_str());
        return false;
    }

    JsonArrayConst arr = doc["input_bindings"].as<JsonArrayConst>();
    if (arr.isNull()) {
        LOG_WARN("CFG", "input_bindings.json missing 'input_bindings' array — ignoring file");
        return false;
    }

    for (JsonObjectConst entry : arr) {
        if (s_inputs.count >= CFG_MAX_INPUT_BINDINGS) {
            LOG_WARN("CFG", "input_bindings.json: > %u entries — extras dropped",
                     static_cast<unsigned>(CFG_MAX_INPUT_BINDINGS));
            break;
        }
        CfgInputBinding &b = s_inputs.bindings[s_inputs.count];
        memset(&b, 0, sizeof(b));
        strlcpy(b.id, entry["id"] | "", sizeof(b.id));
        const int pin = entry["pin"] | -1;
        if (pin < 0 || pin > 39) {
            LOG_WARN("CFG", "input binding '%s': pin=%d out of range — dropped", b.id, pin);
            continue;
        }
        if (isPinConflict(static_cast<int8_t>(pin))) {
            LOG_WARN("CFG", "input binding '%s': pin=%d conflicts with TWAI — dropped", b.id, pin);
            continue;
        }
        b.pin = static_cast<int8_t>(pin);
        b.active = parseInputActive(entry["active"] | "low");
        b.pullup = entry["pullup"] | true;
        int debounce = entry["debounce_ms"] | 20;
        if (debounce < 1)
            debounce = 1;
        if (debounce > 500)
            debounce = 500;
        b.debounceMs = static_cast<uint16_t>(debounce);
        b.kind = parseInputPressKind(entry["kind"] | "short");
        JsonObjectConst action = entry["action"].as<JsonObjectConst>();
        if (action.isNull()) {
            LOG_WARN("CFG", "input binding '%s': missing 'action' — dropped", b.id);
            continue;
        }
        parseButtonAction(action, &b.action);
        if (b.action.type == CfgButtonActionType::UNKNOWN) {
            LOG_WARN("CFG", "input binding '%s': unknown / invalid action — dropped", b.id);
            continue;
        }
        strlcpy(b.signal, entry["signal"] | "", sizeof(b.signal));
        s_inputs.count++;
    }

    s_inputs.loaded = true;
    LOG_INFO("CFG", "input_bindings.json loaded: %u bindings", s_inputs.count);
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
    // loadInputBindings depends on the parsed device config for pin-conflict
    // detection, so it must run AFTER loadDevice.
    r.inputsOk = loadInputBindings();
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
const CfgInputBindings &ConfigLoader::getInputBindings() {
    return s_inputs;
}
bool ConfigLoader::reloadAll() {
    LoadResult r = loadAll();
    LOG_INFO("CFG", "Config reloaded: dashboard=%d signals=%d inputs=%d", r.dashboardOk,
             r.signalsOk, r.inputsOk);
    return r.dashboardOk; // Dashboard is mandatory
}

const CfgSignalDef *ConfigLoader::findSignal(const char *name) {
    if (!name || name[0] == '\0' || !s_signals.loaded)
        return nullptr;
    for (uint8_t i = 0; i < s_signals.signalCount; ++i) {
        if (strcmp(s_signals.signals[i].name, name) == 0) {
            return &s_signals.signals[i];
        }
    }
    return nullptr;
}
