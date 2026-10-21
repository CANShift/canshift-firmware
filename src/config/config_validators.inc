
#include "config_loader_internal.h"
#include "config_loader_rust_bridge.h"

#include "app_config.h"
#include "config/board_profile_loader.h"
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
        ErrorStore::push(ERROR_SRC_CONFIG, "VER_MAJOR", detail);
    }
}

void checkDashboardSchemaFloor(const char *fileVersion) {
    constexpr int kOldestRenderableMinor = 25;
    const int firmwareMajor = parseMajorVersion(CONFIG_SCHEMA_VERSION);
    if (parseMajorVersion(fileVersion) != firmwareMajor)
        return;
    if (parseMinorVersion(fileVersion) >= kOldestRenderableMinor)
        return;
    LOG_ERROR("CFG", "dashboard.json schema %s predates grid layouts — re-send it from the tuner",
              fileVersion);
    char detail[52];
    snprintf(detail, sizeof(detail), "dashboard.json file=%s min=%d.%d", fileVersion, firmwareMajor,
             kOldestRenderableMinor);
    ErrorStore::push(ERROR_SRC_CONFIG, "VER_OLD", detail);
}

struct TopBarItemKindSlug {
    const char *slug;
    TopBarItemKind kind;
};

constexpr TopBarItemKindSlug kTopBarItemKindSlugs[] = {
    {"statusDot", TopBarItemKind::STATUS_DOT}, {"label", TopBarItemKind::LABEL},
    {"separator", TopBarItemKind::SEPARATOR},  {"signal", TopBarItemKind::SIGNAL},
    {"bleIcon", TopBarItemKind::BLE_ICON},     {"themeToggle", TopBarItemKind::THEME_TOGGLE},
    {"modeFlag", TopBarItemKind::MODE_FLAG},   {"trackBadge", TopBarItemKind::TRACK_BADGE},
    {"canRate", TopBarItemKind::CAN_RATE},     {"signalMax", TopBarItemKind::SIGNAL_MAX},
    {"timer", TopBarItemKind::TIMER},          {"flagWord", TopBarItemKind::FLAG_WORD},
};

TopBarItemKind parseTopBarItemKind(const char *str) {
    if (!str)
        return TopBarItemKind::UNKNOWN;
    for (const TopBarItemKindSlug &entry : kTopBarItemKindSlugs) {
        if (strcmp(str, entry.slug) == 0)
            return entry.kind;
    }
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

CfgButtonActionType parseButtonActionType(const char *type) {
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
    if (strcmp(type, "timer_control") == 0)
        return CfgButtonActionType::TIMER_CONTROL;
    return CfgButtonActionType::UNKNOWN;
}

struct TimerOpSlug {
    const char *slug;
    CfgTimerOp op;
};

constexpr TimerOpSlug kTimerOpSlugs[] = {
    {"start", CfgTimerOp::START},   {"pause", CfgTimerOp::PAUSE}, {"resume", CfgTimerOp::RESUME},
    {"toggle", CfgTimerOp::TOGGLE}, {"reset", CfgTimerOp::RESET}, {"lap", CfgTimerOp::LAP},
};

struct TimerSourceSlug {
    const char *slug;
    CfgTimerSource source;
};

constexpr TimerSourceSlug kTimerSourceSlugs[] = {
    {"elapsed", CfgTimerSource::Elapsed},
    {"lap", CfgTimerSource::Lap},
    {"best", CfgTimerSource::Best},
    {"last", CfgTimerSource::Last},
    {"lapCount", CfgTimerSource::LapCount},
    {"delta", CfgTimerSource::Delta},
    {"launchSprint", CfgTimerSource::LaunchSprint},
    {"launchStrip", CfgTimerSource::LaunchStrip},
};

CfgTimerOp parseTimerOp(const char *op) {
    if (!op)
        return CfgTimerOp::UNKNOWN;
    for (const TimerOpSlug &entry : kTimerOpSlugs) {
        if (strcmp(op, entry.slug) == 0)
            return entry.op;
    }
    return CfgTimerOp::UNKNOWN;
}

CfgTimerSource parseTimerSource(const char *source) {
    if (!source)
        return CfgTimerSource::Elapsed;
    for (const TimerSourceSlug &entry : kTimerSourceSlugs) {
        if (strcmp(source, entry.slug) == 0)
            return entry.source;
    }
    return CfgTimerSource::Elapsed;
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

struct WidgetTypeName {
    const char *name;
    WidgetType type;
};

constexpr WidgetTypeName kWidgetTypeNames[] = {
    {"gauge", WidgetType::GAUGE},     {"label", WidgetType::LABEL},
    {"warning", WidgetType::WARNING}, {"button", WidgetType::BUTTON},
    {"timer", WidgetType::TIMER},     {"gear", WidgetType::GEAR_IND},
    {"image", WidgetType::IMAGE},     {"shift_light", WidgetType::SHIFT_LIGHT},
    {"g_meter", WidgetType::G_METER},
};

WidgetType parseWidgetType(const char *str) {
    if (!str)
        return WidgetType::UNKNOWN;
    for (const WidgetTypeName &entry : kWidgetTypeNames) {
        if (strcmp(str, entry.name) == 0)
            return entry.type;
    }
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

bool isPinConflict(int8_t pin) {
    const CfgDeviceConfig &dev = s_device;
    const auto &can = canshift::boards::runtimeBoardProfile().can;
    const int8_t tx = dev.loaded && dev.twaiTxPin >= 0 ? dev.twaiTxPin : can.pin_tx;
    const int8_t rx = dev.loaded && dev.twaiRxPin >= 0 ? dev.twaiRxPin : can.pin_rx;
    return pin == tx || pin == rx;
}

}
