#pragma once
// config_types.h — Dashboard configuration domain types
//
// These types mirror the JSON schema defined in shared-core/src/schemas/.
// If the schema changes, update this file.
//
// Note: This is the firmware-side representation (C++ structs).
// The canonical schema and TypeScript types live in shared-core/.

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

// Schema version is injected at build time by scripts/extra_targets.py from
// canshift-core/src/index.ts (CURRENT_SCHEMA_VERSION). Failing the build is
// preferable to drifting from the canonical TypeScript value (issue #203).
#ifndef CONFIG_SCHEMA_VERSION
    #error "CONFIG_SCHEMA_VERSION not defined — extra_targets.py must inject it from canshift-core"
#endif

// Maximum number of top bar items rendered from `topBar.layout`. Items beyond
// this cap are dropped at parse time with a LOG_WARN.
#define CFG_MAX_TOPBAR_ITEMS 16

// Maximum number of actions a single button widget can fire on press.
// Studio writes ButtonWidgetConfig.actions[]; firmware drops any beyond this cap.
#define CFG_MAX_BUTTON_ACTIONS 4

// Maximum string lengths for config values
#define CFG_MAX_ID_LEN 32
#define CFG_MAX_NAME_LEN 32
#define CFG_MAX_SIGNAL_LEN 32
#define CFG_MAX_PATH_LEN 48
#define CFG_MAX_COLOR_LEN 8 // "#RRGGBB\0"

// ---------------------------------------------------------------------------
// Color (RGB hex string → uint32_t)
// ---------------------------------------------------------------------------
struct CfgColor {
    uint32_t rgb; // 0x00RRGGBB
};

// ---------------------------------------------------------------------------
// Widget type enum
// ---------------------------------------------------------------------------
enum class WidgetType : uint8_t {
    UNKNOWN = 0,
    GAUGE = 1,    // Arc-based gauge (RPM, boost, etc.)
    LABEL = 2,    // Text label showing a signal value
    WARNING = 3,  // Warning indicator light (boolean signal)
    BUTTON = 4,   // Tap action button (page nav, etc.)
    TOP_BAR = 5,  // Reserved for top bar use
    TIMER = 6,    // Lap/session timer
    BAR = 7,      // Horizontal or vertical progress bar
    GEAR_IND = 8, // Large gear indicator
    IMAGE = 9,    // Static or signal-driven image/icon
};

// ---------------------------------------------------------------------------
// Widget layout (position and size in pixels)
// ---------------------------------------------------------------------------
struct CfgLayout {
    int16_t x; // Left edge from screen left
    int16_t y; // Top edge from screen top (0 = below top bar)
    int16_t w; // Width in pixels
    int16_t h; // Height in pixels
    uint8_t zOrder;
};

// ---------------------------------------------------------------------------
// Widget style (colors, fonts)
// ---------------------------------------------------------------------------
struct CfgStyle {
    CfgColor primaryColor;   // Main value/indicator color
    CfgColor secondaryColor; // Background or track color
    CfgColor warningColor;   // Warning state color (default orange)
    CfgColor criticalColor;  // Critical state color (default red)
    CfgColor textColor;      // Label text color
    uint8_t fontSize;        // Font size (must be an enabled LV font size)
    bool hasBorder;          // True if borderColor is active
    CfgColor borderColor;    // Widget container border (1 px, only if hasBorder)
};

// ---------------------------------------------------------------------------
// Widget config (type-specific parameters)
// ---------------------------------------------------------------------------
// Position of the optional widget label drawn at a corner (gauge / bar widgets).
enum class CfgLabelPos : uint8_t {
    TOP_LEFT = 0,
    TOP_CENTER = 1,
    TOP_RIGHT = 2,
    BOTTOM_LEFT = 3,
    BOTTOM_CENTER = 4,
    BOTTOM_RIGHT = 5,
};

// Arc fill rendering mode (issue #175). Mirrors GaugeArcFillStyle in
// canshift-core/src/types/dashboard.ts. Defaults to ZONES so existing arc
// gauges keep the warn/danger sector tinting.
enum class CfgArcFillStyle : uint8_t {
    ZONES = 0,    // Legacy: warn/danger sector tinting on the background track
    GRADIENT = 1, // Single value arc tinted green→orange→red across the range
};

struct CfgGaugeParams {
    float minValue;
    float maxValue;
    float warningLevel;
    float dangerLevel;
    float alertThreshold; // NaN = disabled (issue #133)
    bool showArc;
    bool revFlash; // Pulse the widget red when value reaches revLimitRpm (issue #204)
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[16];              // Unit label shown below the value (e.g. "RPM", "°C")
    char label[CFG_MAX_NAME_LEN]; // Optional widget label drawn at a corner
    CfgLabelPos labelPosition;
    CfgArcFillStyle arcFillStyle; // Arc style only — ignored otherwise (issue #175)
};

struct CfgBarParams {
    float minValue;
    float maxValue;
    float warningLevel;   // Value at which indicator turns warning color
    float dangerLevel;    // Value at which indicator turns critical color
    float alertThreshold; // NaN = disabled (issue #133)
    bool isVertical;      // true = bottom-up fill, false = left-to-right
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[8];
    char label[CFG_MAX_NAME_LEN]; // Optional widget label (e.g. "Boost")
    CfgLabelPos labelPosition;
    char iconName[16]; // SensorIconName key, "" = none
};

struct CfgLabelParams {
    uint8_t decimalPlaces;
    char prefix[16];
    char suffix[16];
    bool hideWhenInvalid;
    float alertThreshold;         // NaN = disabled (issue #133)
    char label[CFG_MAX_NAME_LEN]; // Optional widget label drawn at a corner
    CfgLabelPos labelPosition;
};

struct CfgWarningParams {
    bool invertLogic;             // True = lit when signal == 0 (e.g. oil pressure OK light)
    float threshold;              // Signal value that activates warning
    char iconName[16];            // SensorIconName key, "" = default warning glyph
    char label[CFG_MAX_NAME_LEN]; // Optional widget label drawn at a corner
    CfgLabelPos labelPosition;
};

// Button action types — mirror ButtonAction discriminated union in
// canshift-core/src/types/dashboard.ts. Unknown / unsupported types are
// dropped at parse time with a LOG_WARN.
enum class CfgButtonActionType : uint8_t {
    UNKNOWN = 0,
    NAV_PAGE,   // Navigate to dashboard page
    MAP_SWITCH, // Ask ECU to switch to a specific map slot
    CAN_RAW,    // Send raw CAN frame
};

struct CfgButtonAction {
    CfgButtonActionType type;
    // nav_page payload
    char pageId[CFG_MAX_ID_LEN];
    // map_switch payload
    uint8_t mapIndex;
    // can_raw payload — frame ID + decoded byte payload (canDataLen ≤ 8).
    // Empty data (canDataLen==0) is a legal CAN frame and is preserved.
    uint32_t canFrameId;
    uint8_t canData[8];
    uint8_t canDataLen;
    // Optional disarm payload sent when a toggle button turns OFF (dataOff in JSON).
    // canDataOffLen==0 means no disarm frame is sent.
    uint8_t canDataOff[8];
    uint8_t canDataOffLen;
    // True when the user requested a 29-bit extended ID (issue #319). Auto-set
    // when canFrameId exceeds the 11-bit standard range (>0x7FF) so legacy
    // configs without the flag still transmit valid frames.
    bool canExtended;
};

struct CfgButtonParams {
    char label[CFG_MAX_NAME_LEN];
    char iconPath[CFG_MAX_PATH_LEN];
    char iconName[16]; // SensorIconName key, "" = none
    bool isToggle;     // true = stays active after press; false = momentary
    bool showIcon;
    bool showLabel;
    bool hasColors;       // True when `colors` block was present in JSON
    CfgColor colorNormal; // Idle background tint
    CfgColor colorActive; // Pressed / hover / triggered tint
    uint8_t actionsCount;
    CfgButtonAction actions[CFG_MAX_BUTTON_ACTIONS];
};

struct CfgTimerParams {
    bool autoStart;
    bool formatMsec;              // true = "ss.mmm", false = "mm:ss"
    char label[CFG_MAX_NAME_LEN]; // Optional widget label drawn at a corner
    CfgLabelPos labelPosition;
};

struct CfgImageParams {
    char imagePath[CFG_MAX_PATH_LEN]; // SPIFFS path e.g. "/images/bg.bmp"
    char label[CFG_MAX_NAME_LEN];     // Optional widget label drawn at a corner
    CfgLabelPos labelPosition;
};

// ---------------------------------------------------------------------------
// Widget definition
// ---------------------------------------------------------------------------
struct CfgWidget {
    char id[CFG_MAX_ID_LEN];
    WidgetType type;
    char signalId[CFG_MAX_SIGNAL_LEN]; // Signal name from signals.json
    CfgLayout layout;
    CfgStyle style;

    // Type-specific config — only one is active based on `type`
    union {
        CfgGaugeParams gauge;
        CfgLabelParams label;
        CfgWarningParams warning;
        CfgButtonParams button;
        CfgBarParams bar;
        CfgTimerParams timer;
        CfgImageParams image;
    };
};

// ---------------------------------------------------------------------------
// Page definition
// ---------------------------------------------------------------------------
struct CfgPage {
    char id[CFG_MAX_ID_LEN];
    char bgImagePath[CFG_MAX_PATH_LEN]; // Empty = no image
    CfgColor bgColor;
    bool showTopBar;
    bool visible; // false = page hidden on device (still editable in studio); default true
    uint8_t widgetCount;
    CfgWidget widgets[CONFIG_MAX_WIDGETS_PER_PAGE];
};

// ---------------------------------------------------------------------------
// Top bar item — one entry in the layout array
// ---------------------------------------------------------------------------
enum class TopBarItemKind : uint8_t {
    UNKNOWN = 0,
    STATUS_DOT,   // Coloured dot tied to a signal's freshness ("rpm" or "any")
    LABEL,        // Static text
    SEPARATOR,    // Vertical "|"
    SIGNAL,       // Live signal value with printf-style format
    USB_ICON,     // Download arrow — green when host active
    BLE_ICON,     // "BLE" text badge: blue=connected, dim=advertising, gray=off
    THEME_TOGGLE, // ☀/☾ tap target — only meaningful when hasDayTheme
    MODE_FLAG,    // Text badge — amber when signal ≠ 0, near-black when 0 or invalid
};

enum class TopBarItemPos : uint8_t {
    LEFT = 0,
    CENTER = 1,
    RIGHT = 2,
};

struct CfgTopBarItem {
    TopBarItemKind kind;
    TopBarItemPos position;
    char signalId[CFG_MAX_SIGNAL_LEN]; // statusDot, signal — empty otherwise
    char text[16];                     // label.text — empty otherwise
    char format[16];                   // signal.format (printf-style) — empty otherwise
};

// ---------------------------------------------------------------------------
// Top bar config
// ---------------------------------------------------------------------------
struct CfgTopBar {
    uint8_t height; // Pixels (default 30)
    CfgColor bgColor;
    CfgColor textColor;
    uint8_t itemCount;
    CfgTopBarItem items[CFG_MAX_TOPBAR_ITEMS];
};

// ---------------------------------------------------------------------------
// Day theme preset — stored at dashboard root, mirrors TypeScript ThemePreset
// ---------------------------------------------------------------------------
struct CfgDayTheme {
    CfgColor bgColor; // Page background for day mode
};

// ---------------------------------------------------------------------------
// Dashboard config (root)
// ---------------------------------------------------------------------------
struct CfgDashboard {
    char version[16];
    char name[CFG_MAX_NAME_LEN];
    char defaultPageId[CFG_MAX_ID_LEN];
    float revLimitRpm; // For alert engine
    CfgTopBar topBar;
    // Optional day theme — when present the top bar shows a ☀/N toggle.
    // The active mode is persisted in NVS so it survives power cycles.
    bool hasDayTheme;
    CfgDayTheme dayTheme;
    uint8_t pageCount;
    CfgPage pages[CONFIG_MAX_PAGES];
    bool loaded; // True if successfully loaded from JSON
};

// ---------------------------------------------------------------------------
// Color ramp (issue #430) — value→color mapping driving widget renderers.
// `count == 0` means "no explicit ramp configured" — callers fall back to the
// sensor-name heuristic in src/ui/sensor_color_ramp.h.
// ---------------------------------------------------------------------------
enum class CfgRampInterp : uint8_t {
    Linear = 0,
    Step = 1,
};

struct CfgRampStopDef {
    float value;
    uint32_t color; // 0x00RRGGBB
};

struct CfgColorRampDef {
    uint8_t count;
    CfgRampInterp interpolate;
    CfgRampStopDef stops[CFG_MAX_RAMP_STOPS];
};

// ---------------------------------------------------------------------------
// Signal definition (from signals.json)
// ---------------------------------------------------------------------------
struct CfgSignalDef {
    char name[CFG_MAX_SIGNAL_LEN]; // Must match WidgetConfig.signalId
    uint32_t canFrameId;
    uint8_t startByte;
    uint8_t byteLength; // 1, 2, or 4
    bool bigEndian;
    bool isSigned;
    float scale;
    float offset;
    char unit[16];
    float minValue;
    float maxValue;
    float
        warningLevel; // NAN = not configured; for low-side alerts (oil pressure) the engine inverts
    float dangerLevel; // NAN = not configured
    float
        highWarningLevel; // NAN = not configured; high-side warn threshold (e.g. battery overcharge)
    float highDangerLevel; // NAN = not configured; high-side danger threshold
    uint32_t timeoutMs;
    uint8_t bitMask; // Bitmask for flag signals (0 = full value; non-zero = extract bit)
    // Optional per-signal color ramp (issue #430). `colorRamp.count == 0` means
    // "no ramp configured" → widgets fall back to default sensor-name lookup.
    CfgColorRampDef colorRamp;
};

// ---------------------------------------------------------------------------
// Outbound CAN frame overrides (issue #317).
// Mirrors signals.json `out` block. A zero `*FrameId` means "not configured" —
// callers fall back to the baked default in include/can_signals_out.h.
// ---------------------------------------------------------------------------
struct CfgSignalsOut {
    uint32_t mapSwitchFrameId; // 0 = use baked default
    bool mapSwitchExtended;    // True when ID is 29-bit (auto-set when >0x7FF)
};

struct CfgSignalConfig {
    char version[16];
    char protocol[32]; // Informational tag (e.g. "custom_v1.0"); not used in parsing
    uint32_t canSpeedKbps;
    uint8_t signalCount;
    CfgSignalDef signals[CONFIG_MAX_SIGNALS];
    CfgSignalsOut out;
    bool loaded;
};

// ---------------------------------------------------------------------------
// Device hardware config (from device.json)
// Overrides board_config.h compile-time defaults at runtime.
// ---------------------------------------------------------------------------
struct CfgDeviceConfig {
    uint32_t canSpeedKbps; // CAN bus speed — overrides signals.json if loaded
    int8_t twaiTxPin;      // ESP32 TWAI TX GPIO (-1 = use board_config.h default)
    int8_t twaiRxPin;      // ESP32 TWAI RX GPIO (-1 = use board_config.h default)
    bool loaded;
};
