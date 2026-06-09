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
#define CFG_MAX_SIGNAL_LEN 64
// Target screen profile id length. Profile ids today (`crowpanel-28`) fit in
// 12 chars; 24 leaves headroom for `crowpanel-50` / similar additions when
// the second board lands (issues #17, #18). Single source of truth in
// canshift-core/src/schemas/screen-profile.ts (ScreenProfileIdSchema).
#define CFG_MAX_PROFILE_ID_LEN 24
// Asset / config file paths. 48 was tight — `/assets/sensor_oil_pressure.bin`
// already takes 30 chars and any moderately-named user icon (e.g.
// `/assets/longish-icon-name.bin`, 35 chars) would graze the cap. 64 gives
// comfortable headroom at a cost of ~16 extra bytes per widget/page path
// field — negligible vs. the 1.5 MB flash budget. Issue #909.
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
    // When false, the widget keeps `textColor` regardless of day/night mode.
    // Default true (set by the parser even when the field is absent from the
    // JSON) preserves the v0.7.0 behaviour from #171. Issue #191.
    bool respectDayMode;
};

// ---------------------------------------------------------------------------
// Widget config (type-specific parameters)
// ---------------------------------------------------------------------------

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
    // Single threshold above which the gauge turns red / palette-warning
    // colour (issue #965). The legacy `warningLevel` was removed when the
    // two-zone palette (#954) made it redundant.
    float dangerLevel;
    float alertThreshold; // NaN = disabled (issue #133)
    bool showArc;
    bool revFlash; // Pulse the widget red when value reaches revLimitRpm (issue #204)
    uint8_t decimalPlaces;
    char prefix[8];
    char suffix[16];              // Unit label shown below the value (e.g. "RPM", "°C")
    CfgArcFillStyle arcFillStyle; // Arc style only — ignored otherwise (issue #175)
    // SensorIconName key driving the two-zone semantic palette (issue #954).
    // "" = no palette; widgets fall back to the legacy `style.primaryColor`
    // and zone tints.
    char iconName[16];
};

struct CfgLabelParams {
    uint8_t decimalPlaces;
    char prefix[16];
    char suffix[16];
    float alertThreshold; // NaN = disabled (issue #133)
};

struct CfgWarningParams {
    bool invertLogic;  // True = lit when signal == 0 (e.g. oil pressure OK light)
    float threshold;   // Signal value that activates warning
    char iconName[16]; // SensorIconName key, "" = default warning glyph
};

// Button action types — mirror ButtonAction discriminated union in
// canshift-core/src/types/dashboard.ts. Unknown / unsupported types are
// dropped at parse time with a LOG_WARN.
enum class CfgButtonActionType : uint8_t {
    UNKNOWN = 0,
    NAV_PAGE,       // Navigate to dashboard page
    MAP_SWITCH,     // Ask ECU to switch to a specific map slot
    CAN_RAW,        // Send raw CAN frame
    CRUISE_CONTROL, // Cruise-control op (issue #833, full integration #451)
};

// Cruise-control operations. Mirrors CruiseControlOp in canshift-core. The
// firmware currently logs the op and (where applicable) flips a shared
// `cruise_armed` signal so any on-screen widget bound to it can render the
// state. Real CAN frames land with issue #451 once per-ECU support is known.
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
    // cruise_control payload (issue #833). stepKmh==0 means "firmware default".
    CfgCruiseOp cruiseOp;
    uint8_t cruiseStepKmh;
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
        CfgTimerParams timer;
        CfgImageParams image;
    };
};

// ---------------------------------------------------------------------------
// Page rendering template — mirrors PageTemplate in canshift-core (issue #451).
// `CUSTOM` is the legacy free-form widget grid; other values cause PageManager
// to draw a procedural layout and ignore the `widgets[]` array.
// ---------------------------------------------------------------------------
enum class CfgPageTemplate : uint8_t {
    CUSTOM = 0,     // Default — render widgets[] as-is
    CRUISE_CONTROL, // 2×2 grid of (+, −, SET, OFF) buttons (issue #451)
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
    // Page rendering template (issue #451). Defaults to CUSTOM for back-compat
    // — every dashboard config written before #451 lacks the JSON field and
    // must continue to render its widgets[] grid unchanged.
    CfgPageTemplate templateKind;
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
    TRACK_BADGE,  // "TRACK" text — lit while the mobile app pushes trackMode=true (#844)
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
    // Screen profile the dashboard was authored against (issues #17, #18).
    // Drives the design→physical coordinate scaling computed at boot in
    // `ui/screen_profile.cpp`. Empty / missing in JSON means "fall back to
    // DEFAULT_SCREEN_PROFILE_ID" — mirrors resolveScreenProfile() in
    // canshift-core/src/schemas/screen-profile.ts.
    char targetProfile[CFG_MAX_PROFILE_ID_LEN];
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
    // OBD-II polling (issue #841 — phase 3 of #556). When `pollIntervalMs > 0`,
    // the signal is request/response: Obd2Poller sends a query frame at the
    // configured interval and decodes the response. `pollIntervalMs == 0`
    // (the parser default for legacy configs) keeps the signal in passive
    // broadcast mode — `canFrameId` is the unsolicited frame to decode.
    // v1 ships Mode 01 only at the standard 0x7DF/0x7E8 pair.
    uint8_t pollMode;        // OBD-II mode byte (0x01); 0 = polling disabled
    uint8_t pollPid;         // OBD-II PID byte (Mode 01: see SAE J1979)
    uint32_t pollIntervalMs; // 0 = polling disabled (legacy broadcast behaviour)
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

// ---------------------------------------------------------------------------
// Physical GPIO input bindings (from input_bindings.json) — issue #833.
// Each entry wires one GPIO press (with debounce + press kind) to one
// dashboard action, reusing the existing CfgButtonAction shape.
// ---------------------------------------------------------------------------

// Hard cap mirrored from canshift-core MAX_INPUT_BINDINGS. Entries beyond
// this are dropped at parse time with a LOG_WARN.
#define CFG_MAX_INPUT_BINDINGS 16

enum class CfgInputActive : uint8_t {
    // Avoid the names LOW/HIGH — Arduino defines those as numeric macros.
    ACTIVE_LOW = 0,  // Button pulls pin to GND when pressed (default; needs pullup)
    ACTIVE_HIGH = 1, // Button drives pin to Vcc when pressed
};

enum class CfgInputPressKind : uint8_t {
    UNKNOWN = 0,
    SHORT,
    LONG,
    DOUBLE,
};

struct CfgInputBinding {
    char id[CFG_MAX_ID_LEN];
    int8_t pin;            // ESP32 input-capable GPIO (-1 = disabled)
    CfgInputActive active; // Activation level
    bool pullup;           // Enable internal pullup (paired with ACTIVE_LOW)
    uint16_t debounceMs;
    CfgInputPressKind kind;
    CfgButtonAction action;
    // Optional signal name shared with an on-screen toggle button widget.
    // When set, firing this binding writes the FLIPPED current signal value
    // into the SignalStore so the on-screen widget syncs without waiting for
    // an ECU echo. "" = no shared signal (no cross-widget sync).
    char signal[CFG_MAX_SIGNAL_LEN];
};

struct CfgInputBindings {
    uint8_t count;
    CfgInputBinding bindings[CFG_MAX_INPUT_BINDINGS];
    bool loaded; // True if input_bindings.json existed and parsed
};
