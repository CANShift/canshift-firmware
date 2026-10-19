#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"
#include "config_enums.h"

#ifndef CONFIG_SCHEMA_VERSION
    #error "CONFIG_SCHEMA_VERSION not defined — extra_targets.py must inject it from canshift-core"
#endif

constexpr int32_t kCfgMaxTopbarItems = 16;

constexpr int32_t kCfgMaxButtonActions = 4;

constexpr int32_t kCfgMaxCycleStates = 4;

constexpr int32_t kCfgMaxIdLen = 32;
constexpr int32_t kCfgMaxNameLen = 32;
constexpr int32_t kCfgMaxSignalLen = 64;
constexpr int32_t kCfgMaxExprLen = 128;
constexpr int32_t kCfgMaxExprRefs = 8;
constexpr int32_t kCfgMaxProfileIdLen = 24;
constexpr int32_t kCfgMaxPathLen = 64;

struct CfgColor {
    uint32_t rgb;
};

struct CfgLayout {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint8_t col;
    uint8_t colSpan;
    uint8_t row;
    uint8_t rowSpan;
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

struct CfgGaugeParams {
    uint8_t big;
    float minValue;
    float maxValue;
    float warnLevel;
    float dangerLevel;
    bool dangerBelow;
    bool revFlash;
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[16];
    char iconName[16];
    char kicker[kCfgMaxNameLen];
};

struct CfgLabelParams {
    uint8_t big;
    uint8_t decimalPlaces;
    char prefix[16];
    char suffix[16];
    char kicker[kCfgMaxNameLen];
    float warnLevel;
    float dangerLevel;
    bool dangerBelow;
    bool showBar;
    bool revFlash;
    float minValue;
    float maxValue;
};

struct CfgWarningParams {
    bool invertLogic;
    float threshold;
    char iconName[16];
};

struct CfgButtonAction {
    CfgButtonActionType type;
    char pageId[kCfgMaxIdLen];
    uint8_t mapIndex;
    uint32_t canFrameId;
    uint8_t canData[8];
    uint8_t canDataLen;
    uint8_t canDataOff[8];
    uint8_t canDataOffLen;
    bool canExtended;
    CfgCruiseOp cruiseOp;
    uint8_t cruiseStepKmh;
    CfgTimerOp timerOp;
};

struct CfgButtonState {
    char label[kCfgMaxNameLen];
    bool hasColors;
    CfgColor colorNormal;
    CfgColor colorActive;
    CfgButtonAction action;
};

struct CfgButtonParams {
    CfgButtonMode mode;
    char label[kCfgMaxNameLen];
    char kicker[kCfgMaxNameLen];
    bool isToggle;
    bool showLabel;
    bool hasColors;
    CfgColor colorNormal;
    CfgColor colorActive;
    uint8_t actionsCount;
    CfgButtonAction actions[kCfgMaxButtonActions];
    uint8_t statesCount;
    uint8_t initialActiveIndex;
    CfgButtonState states[kCfgMaxCycleStates];
};

struct CfgTimerParams {
    uint8_t big;
    bool autoStart;
    bool formatMsec;
    CfgTimerSource source;
};

struct CfgShiftLightParams {
    float startValue;
    uint8_t redSegments;
};

struct CfgImageParams {
    char imagePath[kCfgMaxPathLen];
};

struct CfgWidget {
    char id[kCfgMaxIdLen];
    WidgetType type;
    char signalId[kCfgMaxSignalLen];
    CfgLayout layout;
    CfgStyle style;

    union {
        CfgGaugeParams gauge;
        CfgLabelParams label;
        CfgWarningParams warning;
        CfgTimerParams timer;
        CfgImageParams image;
        CfgShiftLightParams shiftLight;
    };
    CfgButtonParams *button;
};

struct CfgTopBarItem {
    TopBarItemKind kind;
    TopBarItemPos position;
    char signalId[kCfgMaxSignalLen];
    char text[16];
    char format[16];
};

struct CfgStatusRow {
    bool hasCenter;
    CfgTopBarItem center;
    bool hasRight;
    CfgTopBarItem right;
};

struct CfgPage {
    char id[kCfgMaxIdLen];
    char bgImagePath[kCfgMaxPathLen];
    CfgColor bgColor;
    bool showTopBar;
    bool visible;
    CfgPageTemplate templateKind;
    CfgStatusRow statusRow;
    uint8_t widgetCount;
    uint8_t widgetCapacity;
    CfgWidget *widgets;
};

struct CfgTopBar {
    uint8_t height;
    CfgColor bgColor;
    CfgColor textColor;
    uint8_t itemCount;
    CfgTopBarItem items[kCfgMaxTopbarItems];
};

struct CfgTheme {
    CfgColor bgColor;
    bool hasPalette;
    CfgColor text;
    CfgColor textDim;
};

struct CfgCriticalAlert {
    bool armed;
    char signalId[kCfgMaxSignalLen];
    float firesAt;
    bool below;
};

struct CfgPageButtons {
    char next[kCfgMaxSignalLen];
    char previous[kCfgMaxSignalLen];
};

struct CfgDashboard {
    char version[16];
    char name[kCfgMaxNameLen];
    char defaultPageId[kCfgMaxIdLen];
    float revLimitRpm;
    CfgUnitSystem units = CfgUnitSystem::METRIC;
    char targetProfile[kCfgMaxProfileIdLen];
    CfgTopBar topBar;
    bool hasTheme;
    CfgTheme dayFace;
    CfgTheme nightFace;
    char dayNightSignal[kCfgMaxIdLen];
    uint8_t criticalAlertCount;
    CfgCriticalAlert criticalAlerts[kConfigMaxCriticalAlerts];
    CfgPageButtons pageButtons;
    uint8_t pageCount;
    CfgPage pages[kConfigMaxPages];
    bool loaded;

    ~CfgDashboard() {
        for (auto &page : pages) {
            for (uint8_t i = 0; i < page.widgetCapacity; ++i) {
                delete page.widgets[i].button;
            }
            delete[] page.widgets;
            page.widgets = nullptr;
        }
    }
    CfgDashboard() = default;
    CfgDashboard(const CfgDashboard &) = delete;
    CfgDashboard &operator=(const CfgDashboard &) = delete;
};

struct CfgSignalDef {
    char name[kCfgMaxSignalLen];
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
    uint8_t pollMode;
    uint8_t pollPid;
    uint32_t pollIntervalMs;
    char expr[kCfgMaxExprLen];
    uint16_t targetId;
    bool hasTargetId;
    uint8_t exprRefCount;
    uint16_t exprRefs[kCfgMaxExprRefs];
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
    CfgSignalDef signals[kConfigMaxSignals];
    CfgSignalsOut out;
    bool loaded;
};

struct CfgDeviceConfig {
    uint32_t canSpeedKbps;
    int8_t twaiTxPin;
    int8_t twaiRxPin;
    bool loaded;
};

constexpr int32_t kCfgMaxInputBindings = 16;

struct CfgInputBinding {
    char id[kCfgMaxIdLen];
    int8_t pin;
    CfgInputActive active;
    bool pullup;
    uint16_t debounceMs;
    CfgInputPressKind kind;
    CfgButtonAction action;
    char signal[kCfgMaxSignalLen];
};

struct CfgInputBindings {
    uint8_t count;
    CfgInputBinding bindings[kCfgMaxInputBindings];
    bool loaded;
};
