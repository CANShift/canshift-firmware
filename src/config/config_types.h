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

struct CfgGaugeParams {
    float minValue;
    float maxValue;
    float warningLevel;
    float dangerLevel;
    float alertThreshold; // NaN = disabled (issue #133)
    uint8_t numTicks;
    bool showNeedle;
    bool showArc;
    bool revFlash; // Pulse the widget red when value reaches revLimitRpm (issue #204)
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[16];              // Unit label shown below the value (e.g. "RPM", "°C")
    char label[CFG_MAX_NAME_LEN]; // Optional widget label drawn at a corner
    CfgLabelPos labelPosition;
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
};

struct CfgButtonParams {
    char label[CFG_MAX_NAME_LEN];
    char iconPath[CFG_MAX_PATH_LEN];
    char iconName[16];     // SensorIconName key, "" = none
    bool isToggle;         // true = stays active after press; false = momentary
    bool showIcon;
    bool showLabel;
    bool hasColors;        // True when `colors` block was present in JSON
    CfgColor colorNormal;  // Idle background tint
    CfgColor colorActive;  // Pressed / hover / triggered tint
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
// Per-page color palette — widgets inherit these unless overridden individually
// ---------------------------------------------------------------------------
struct CfgPagePalette {
    CfgColor surface; // Widget card/surface background
    CfgColor primary; // Gauge arcs, bar fills, highlights
    CfgColor accent;  // Secondary highlight
    CfgColor text;    // Default widget text
    CfgColor textDim; // Muted / secondary text
    CfgColor warning; // Warning threshold indicator
    CfgColor danger;  // Danger threshold indicator
    CfgColor success; // Normal / ok state
};

// ---------------------------------------------------------------------------
// Page definition
// ---------------------------------------------------------------------------
struct CfgPage {
    char id[CFG_MAX_ID_LEN];
    char bgImagePath[CFG_MAX_PATH_LEN]; // Empty = no image
    CfgColor bgColor;
    CfgPagePalette palette;
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
    THEME_TOGGLE, // ☀/☾ tap target — only meaningful when hasDayTheme
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
    uint8_t height; // Pixels (default 24)
    CfgColor bgColor;
    CfgColor textColor;
    bool hasLayout; // True if `topBar.layout` was present in dashboard.json
    uint8_t itemCount;
    CfgTopBarItem items[CFG_MAX_TOPBAR_ITEMS];
};

// ---------------------------------------------------------------------------
// Day theme preset — stored at dashboard root, mirrors TypeScript ThemePreset
// ---------------------------------------------------------------------------
struct CfgDayTheme {
    CfgColor bgColor;       // Page background for day mode
    CfgPagePalette palette; // Widget palette for day mode
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
    uint32_t timeoutMs;
    uint8_t bitMask; // Bitmask for flag signals (0 = full value; non-zero = extract bit)
};

struct CfgSignalConfig {
    char version[16];
    char protocol[32]; // e.g. "maxxecu_v1.2"
    uint32_t canSpeedKbps;
    uint8_t signalCount;
    CfgSignalDef signals[CONFIG_MAX_SIGNALS];
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
