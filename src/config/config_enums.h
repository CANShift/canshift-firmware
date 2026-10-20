#pragma once

#include <stdint.h>

enum class WidgetType : uint8_t {
    UNKNOWN = 0,
    GAUGE = 1,
    LABEL = 2,
    WARNING = 3,
    BUTTON = 4,
    TOP_BAR = 5,
    TIMER = 6,
    GEAR_IND = 8,
    IMAGE = 9,
    SHIFT_LIGHT = 10,
    G_METER = 11,
};

enum class CfgButtonActionType : uint8_t {
    UNKNOWN = 0,
    NAV_PAGE,
    MAP_SWITCH,
    CAN_RAW,
    CRUISE_CONTROL,
    TIMER_CONTROL,
};

enum class CfgTimerOp : uint8_t {
    UNKNOWN = 0,
    START,
    PAUSE,
    RESUME,
    TOGGLE,
    RESET,
    LAP,
};

enum class CfgTimerSource : uint8_t {
    Elapsed = 0,
    Lap,
    Best,
    Last,
    LapCount,
    Delta,
    LaunchSprint,
    LaunchStrip,
};

enum class CfgCruiseOp : uint8_t {
    UNKNOWN = 0,
    ON,
    OFF,
    TOGGLE,
    SET,
    RESUME,
    INCREMENT,
    DECREMENT,
};

enum class CfgButtonMode : uint8_t {
    SINGLE = 0,
    CYCLE = 1,
};

enum class CfgPageTemplate : uint8_t {
    CUSTOM = 0,
    CRUISE_CONTROL,
};

enum class TopBarItemKind : uint8_t {
    UNKNOWN = 0,
    STATUS_DOT,
    LABEL,
    SEPARATOR,
    SIGNAL,
    BLE_ICON,
    THEME_TOGGLE,
    MODE_FLAG,
    TRACK_BADGE,
    CAN_RATE,
    SIGNAL_MAX,
};

enum class TopBarItemPos : uint8_t {
    LEFT = 0,
    CENTER = 1,
    RIGHT = 2,
};

enum class CfgUnitSystem : uint8_t {
    METRIC = 0,
    IMPERIAL = 1,
};

enum class CfgInputActive : uint8_t {
    ACTIVE_LOW = 0,
    ACTIVE_HIGH = 1,
};

enum class CfgInputPressKind : uint8_t {
    UNKNOWN = 0,
    SHORT,
    LONG,
    DOUBLE,
};
