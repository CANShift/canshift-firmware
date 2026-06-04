// config_validators.cpp — Schema-version checks + string-to-enum decoders for
// the ConfigLoader translation units (#1207).
//
// Extracted from `config_loader.cpp`. These helpers are deliberately small
// and pure: each maps a JSON string to a firmware enum (with a documented
// fallback for unknown values) or runs a single cross-field invariant. The
// `parseMajorVersion` backend selector lives in `config_loader_rust_bridge.h`
// so the FFI surface stays isolated; `checkSchemaVersion` here drives it.

#include "config_loader_internal.h"
#include "config_loader_rust_bridge.h"

#include "app_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"

#include <cstdio>
#include <cstring>

namespace ConfigLoaderInternal {

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

// Parse arcFillStyle (issue #175). Defaults to ZONES so legacy configs and
// unknown values keep the previous warn/danger sector tinting behaviour.
CfgArcFillStyle parseArcFillStyle(const char *str) {
    if (!str)
        return CfgArcFillStyle::ZONES;
    if (strcmp(str, "gradient") == 0)
        return CfgArcFillStyle::GRADIENT;
    return CfgArcFillStyle::ZONES;
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
    if (strcmp(str, "gear") == 0)
        return WidgetType::GEAR_IND;
    if (strcmp(str, "image") == 0)
        return WidgetType::IMAGE;
    return WidgetType::UNKNOWN;
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

} // namespace ConfigLoaderInternal
