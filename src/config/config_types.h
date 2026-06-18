#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

#ifndef CONFIG_SCHEMA_VERSION
    #error "CONFIG_SCHEMA_VERSION not defined — extra_targets.py must inject it from canshift-core"
#endif

#define CFG_MAX_TOPBAR_ITEMS 16

#define CFG_MAX_BUTTON_ACTIONS 4

#define CFG_MAX_CYCLE_STATES 4

#define CFG_MAX_ID_LEN 32
#define CFG_MAX_NAME_LEN 32
#define CFG_MAX_SIGNAL_LEN 64
#define CFG_MAX_EXPR_LEN 128
#define CFG_MAX_PROFILE_ID_LEN 24
#define CFG_MAX_PATH_LEN 64
#define CFG_MAX_COLOR_LEN 8

struct CfgColor {
    uint32_t rgb;
};

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
};

struct CfgLayout {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint8_t zOrder;
};

struct CfgStyle {
    CfgColor primaryColor;
    CfgColor secondaryColor;
    CfgColor warningColor;
    CfgColor criticalColor;
    CfgColor textColor;
    uint8_t fontSize;
    bool hasBorder;
    CfgColor borderColor;
    bool respectDayMode;
};

enum class CfgArcFillStyle : uint8_t {
    ZONES = 0,
    GRADIENT = 1,
};

struct CfgGaugeParams {
    float minValue;
    float maxValue;
    float dangerLevel;
    float alertThreshold;
    bool showArc;
    bool revFlash;
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[16];
    CfgArcFillStyle arcFillStyle;
    char iconName[16];
};

struct CfgLabelParams {
    uint8_t decimalPlaces;
    char prefix[16];
    char suffix[16];
    float alertThreshold;
};

struct CfgWarningParams {
    bool invertLogic;
    float threshold;
    char iconName[16];
};

enum class CfgButtonActionType : uint8_t {
    UNKNOWN = 0,
    NAV_PAGE,
    MAP_SWITCH,
    CAN_RAW,
    CRUISE_CONTROL,
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

struct CfgButtonAction {
    CfgButtonActionType type;
    char pageId[CFG_MAX_ID_LEN];
    uint8_t mapIndex;
    uint32_t canFrameId;
    uint8_t canData[8];
    uint8_t canDataLen;
    uint8_t canDataOff[8];
    uint8_t canDataOffLen;
    bool canExtended;
    CfgCruiseOp cruiseOp;
    uint8_t cruiseStepKmh;
};

enum class CfgButtonMode : uint8_t {
    SINGLE = 0,
    CYCLE = 1,
};

struct CfgButtonState {
    char label[CFG_MAX_NAME_LEN];
    char iconName[16];
    bool hasIconName;
    bool hasColors;
    CfgColor colorNormal;
    CfgColor colorActive;
    CfgButtonAction action;
};

struct CfgButtonParams {
    CfgButtonMode mode;
    char label[CFG_MAX_NAME_LEN];
    char iconPath[CFG_MAX_PATH_LEN];
    char iconName[16];
    bool isToggle;
    bool showIcon;
    bool showLabel;
    bool hasColors;
    CfgColor colorNormal;
    CfgColor colorActive;
    uint8_t actionsCount;
    CfgButtonAction actions[CFG_MAX_BUTTON_ACTIONS];
    uint8_t statesCount;
    uint8_t initialActiveIndex;
    CfgButtonState states[CFG_MAX_CYCLE_STATES];
};

struct CfgTimerParams {
    bool autoStart;
    bool formatMsec;
};

struct CfgImageParams {
    char imagePath[CFG_MAX_PATH_LEN];
};

struct CfgWidget {
    char id[CFG_MAX_ID_LEN];
    WidgetType type;
    char signalId[CFG_MAX_SIGNAL_LEN];
    CfgLayout layout;
    CfgStyle style;

    union {
        CfgGaugeParams gauge;
        CfgLabelParams label;
        CfgWarningParams warning;
        CfgButtonParams button;
        CfgTimerParams timer;
        CfgImageParams image;
    };
};

enum class CfgPageTemplate : uint8_t {
    CUSTOM = 0,
    CRUISE_CONTROL,
};

struct CfgPage {
    char id[CFG_MAX_ID_LEN];
    char bgImagePath[CFG_MAX_PATH_LEN];
    CfgColor bgColor;
    bool showTopBar;
    bool visible;
    CfgPageTemplate templateKind;
    uint8_t widgetCount;
    uint8_t widgetCapacity;
    CfgWidget *widgets;
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
};

enum class TopBarItemPos : uint8_t {
    LEFT = 0,
    CENTER = 1,
    RIGHT = 2,
};

struct CfgTopBarItem {
    TopBarItemKind kind;
    TopBarItemPos position;
    char signalId[CFG_MAX_SIGNAL_LEN];
    char text[16];
    char format[16];
};

struct CfgTopBar {
    uint8_t height;
    CfgColor bgColor;
    CfgColor textColor;
    uint8_t itemCount;
    CfgTopBarItem items[CFG_MAX_TOPBAR_ITEMS];
};

struct CfgDayTheme {
    CfgColor bgColor;
};

struct CfgDashboard {
    char version[16];
    char name[CFG_MAX_NAME_LEN];
    char defaultPageId[CFG_MAX_ID_LEN];
    float revLimitRpm;
    char targetProfile[CFG_MAX_PROFILE_ID_LEN];
    CfgTopBar topBar;
    bool hasDayTheme;
    CfgDayTheme dayTheme;
    uint8_t pageCount;
    CfgPage pages[CONFIG_MAX_PAGES];
    bool loaded;

    ~CfgDashboard() {
        for (auto &page : pages) {
            delete[] page.widgets;
            page.widgets = nullptr;
        }
    }
    CfgDashboard() = default;
    CfgDashboard(const CfgDashboard &) = delete;
    CfgDashboard &operator=(const CfgDashboard &) = delete;
};

enum class CfgRampInterp : uint8_t {
    Linear = 0,
    Step = 1,
};

struct CfgRampStopDef {
    float value;
    uint32_t color;
};

struct CfgColorRampDef {
    uint8_t count;
    CfgRampInterp interpolate;
    CfgRampStopDef stops[CFG_MAX_RAMP_STOPS];
};

struct CfgSignalDef {
    char name[CFG_MAX_SIGNAL_LEN];
    uint32_t canFrameId;
    uint8_t startByte;
    uint8_t byteLength;
    bool bigEndian;
    bool isSigned;
    float scale;
    float offset;
    char unit[16];
    float minValue;
    float maxValue;
    float warningLevel;
    float dangerLevel;
    float highWarningLevel;
    float highDangerLevel;
    uint32_t timeoutMs;
    uint8_t bitMask;
    CfgColorRampDef colorRamp;
    uint8_t pollMode;
    uint8_t pollPid;
    uint32_t pollIntervalMs;
    char expr[CFG_MAX_EXPR_LEN];
};

struct CfgSignalsOut {
    uint32_t mapSwitchFrameId;
    bool mapSwitchExtended;
};

struct CfgSignalConfig {
    char version[16];
    char protocol[32];
    uint32_t canSpeedKbps;
    uint8_t signalCount;
    CfgSignalDef signals[CONFIG_MAX_SIGNALS];
    CfgSignalsOut out;
    bool loaded;
};

struct CfgDeviceConfig {
    uint32_t canSpeedKbps;
    int8_t twaiTxPin;
    int8_t twaiRxPin;
    bool loaded;
};

#define CFG_MAX_INPUT_BINDINGS 16

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

struct CfgInputBinding {
    char id[CFG_MAX_ID_LEN];
    int8_t pin;
    CfgInputActive active;
    bool pullup;
    uint16_t debounceMs;
    CfgInputPressKind kind;
    CfgButtonAction action;
    char signal[CFG_MAX_SIGNAL_LEN];
};

struct CfgInputBindings {
    uint8_t count;
    CfgInputBinding bindings[CFG_MAX_INPUT_BINDINGS];
    bool loaded;
};
