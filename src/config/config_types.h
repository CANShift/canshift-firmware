#pragma once
// config_types.h — Dashboard configuration domain types
//
// These types mirror the JSON schema defined in shared-core/src/schemas/.
// If the schema changes, update this file and bump CONFIG_SCHEMA_VERSION.
//
// Note: This is the firmware-side representation (C++ structs).
// The canonical schema and TypeScript types live in shared-core/.

#include <stdint.h>
#include <stdbool.h>
#include "app_config.h"

// Schema version — must match the "version" field in dashboard.json
#define CONFIG_SCHEMA_VERSION "1.0.0"

// Maximum string lengths for config values
#define CFG_MAX_ID_LEN 32
#define CFG_MAX_NAME_LEN 64
#define CFG_MAX_SIGNAL_LEN 32
#define CFG_MAX_PATH_LEN 64
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
struct CfgGaugeParams {
    float minValue;
    float maxValue;
    float warningLevel;
    float dangerLevel;
    uint8_t numTicks;
    bool showNeedle;
    bool showArc;
    char suffix[16]; // Unit label shown below the value (e.g. "RPM", "°C")
};

struct CfgBarParams {
    float minValue;
    float maxValue;
    float warningLevel; // Value at which indicator turns warning color
    float dangerLevel;  // Value at which indicator turns critical color
    bool isVertical;    // true = bottom-up fill, false = left-to-right
};

struct CfgLabelParams {
    uint8_t decimalPlaces;
    char prefix[16];
    char suffix[16];
    bool hideWhenInvalid;
};

struct CfgWarningParams {
    bool invertLogic; // True = lit when signal == 0 (e.g. oil pressure OK light)
    float threshold;  // Signal value that activates warning
};

struct CfgButtonParams {
    char targetPageId[CFG_MAX_ID_LEN];
    char label[CFG_MAX_NAME_LEN];
    char iconPath[CFG_MAX_PATH_LEN];
};

struct CfgTimerParams {
    bool autoStart;
    bool formatMsec; // true = "ss.mmm", false = "mm:ss"
};

struct CfgImageParams {
    char imagePath[CFG_MAX_PATH_LEN]; // SPIFFS path e.g. "/images/bg.bmp"
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
    char name[CFG_MAX_NAME_LEN];
    char bgImagePath[CFG_MAX_PATH_LEN]; // Empty = no image
    CfgColor bgColor;
    CfgPagePalette palette;
    bool showTopBar;
    uint8_t widgetCount;
    CfgWidget widgets[CONFIG_MAX_WIDGETS_PER_PAGE];
};

// ---------------------------------------------------------------------------
// Top bar config
// ---------------------------------------------------------------------------
struct CfgTopBar {
    uint8_t height; // Pixels (default 24)
    bool showMapName;
    bool showMapProfile;
    CfgColor bgColor;
    CfgColor textColor;
};

// ---------------------------------------------------------------------------
// Day theme preset — stored at dashboard root, mirrors TypeScript ThemePreset
// ---------------------------------------------------------------------------
struct CfgDayTheme {
    CfgColor bgColor;        // Page background for day mode
    CfgPagePalette palette;  // Widget palette for day mode
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
    uint32_t timeoutMs;
    uint8_t bitMask;  // Bitmask for flag signals (0 = full value; non-zero = extract bit)
};

struct CfgSignalConfig {
    char version[16];
    char protocol[32]; // e.g. "maxxecu_v1.2"
    uint32_t canSpeedKbps;
    uint8_t signalCount;
    CfgSignalDef signals[CONFIG_MAX_SIGNALS];
    bool loaded;
};
