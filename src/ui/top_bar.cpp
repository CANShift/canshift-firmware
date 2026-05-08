// top_bar.cpp — Persistent top status bar
//
// Layout (left to right):
//   [• ECU]  [• CAN]  ......  12.4V  ↓  ☀
//
// Settings is opened by swiping down from the top of the screen — there is no
// dedicated gear button (#50, redundant given the gesture).
//
// Status sources:
//   ECU dot:  green when SignalIds::RPM is valid (recent ECU frame received)
//   CAN dot:  green when at least one signal has been received recently
//   Voltage:  SignalIds::BATTERY_VOLTS — formatted "12.4V" (— if unknown)
//   Download: green when UsbComm reports a recent host command (studio attached)

#include "top_bar.h"
#include "ui/font_manager.h"
#include "settings_page.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "hal/usb/usb_comm.h"
#include "diag/logger.h"
#include "util/format_float.h"

#include <lvgl.h>
#include <math.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// TopBar proportion table — MIRROR OF canshift-core/src/topbar-metrics.ts
//
// Both the Studio preview and this renderer must agree pixel-for-pixel given
// the same bar height. The TS file is the canonical source of truth — if you
// edit a ratio here, edit it there too (and bump the canshift-core unit test).
// ---------------------------------------------------------------------------

namespace TopBarMetrics {
constexpr float DOT_RATIO = 0.30f;       // status-dot diameter / bar height
constexpr float FONT_SIZE_RATIO = 0.45f; // label font size / bar height
constexpr float SEPARATOR_RATIO = 0.55f; // separator glyph height / bar height
constexpr float GAP_RATIO = 0.25f;       // inter-item gap / bar height
constexpr float PADDING_RATIO = 0.40f;   // outer pad inside the bar / bar height
constexpr float ICON_SIZE_RATIO = 1.15f; // icon font size / label font size
} // namespace TopBarMetrics

// Match Studio's Math.round (round-half-away-from-zero). C++ lroundf is the
// correct primitive — std::round() returns float, lroundf returns long.
static inline int16_t metricRound(float v) {
    return static_cast<int16_t>(lroundf(v));
}
static inline int16_t derivedDot(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::DOT_RATIO);
}
static inline int16_t derivedFontSize(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::FONT_SIZE_RATIO);
}
static inline int16_t derivedSeparator(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::SEPARATOR_RATIO);
}
static inline int16_t derivedGap(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::GAP_RATIO);
}
static inline int16_t derivedPadding(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::PADDING_RATIO);
}
static inline int16_t derivedIconSize(int16_t fontSize) {
    return metricRound(static_cast<float>(fontSize) * TopBarMetrics::ICON_SIZE_RATIO);
}

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static lv_obj_t *s_bar = nullptr;

// Warnings cluster — a small container anchored to the right edge of the bar
// that holds firmware-driven warning badges (NO SD, future: CAN errors, low
// fuel, etc.). Grows leftward as badges are added; stays right-aligned.
//
// The cluster is built once in init() but only made visible when at least
// one warning is active. Each badge is a small label inside the cluster.
//
// Configured items are rendered before init builds the cluster, so the
// cluster always paints on top in the right-most position. A small gap is
// kept from any configured right-aligned item via TopBarItemPos::RIGHT
// alignment offsets.
static lv_obj_t *s_warningsCluster = nullptr;

struct WarningBadge {
    lv_obj_t *obj; // nullptr when inactive
    bool active;
};
static WarningBadge s_warnings[TopBar::WarningKindCount] = {};

// Hardcoded-layout handles. Used when `topBar.layout` is absent (legacy path).
// When the layout is config-driven these stay null and update() walks the
// dynamic-item table instead.
static lv_obj_t *s_ecuDot = nullptr;
static lv_obj_t *s_ecuLabel = nullptr;
static lv_obj_t *s_canDot = nullptr;
static lv_obj_t *s_canLabel = nullptr;

static lv_obj_t *s_voltageLabel = nullptr;
static lv_obj_t *s_usbIcon = nullptr;

static lv_obj_t *s_themeIcon = nullptr;

static int16_t s_height = 30;

// Dynamic item table — populated when `topBar.layout` drives the rendering.
// Each entry stores the LVGL handle plus the metadata needed for update().
struct DynItem {
    TopBarItemKind kind;
    lv_obj_t *obj; // primary lv object (label / dot)
    char signalId[CFG_MAX_SIGNAL_LEN];
    char format[16];
    // Cached last-rendered values — updates are skipped when nothing changed,
    // so unchanged frames cost only a few comparisons (issue #95, fix F3).
    char lastText[16];
    uint32_t lastColor;
    bool lastSeenValid;
};
static DynItem s_dynItems[CFG_MAX_TOPBAR_ITEMS];
static uint8_t s_dynCount = 0;

// Day/night icons live on the SD as 12×12 RGB565 .bin (LVGL native format).
// Source PNGs are in scripts/icon_sources/, regenerable via
// scripts/png_to_lvgl_bin.py. We can't use the Unicode sun/moon glyphs
// because the compile-time Montserrat fonts don't include them.

static constexpr uint32_t COLOR_DOT_OK = 0x33CC44;    // green — connected, fresh data
static constexpr uint32_t COLOR_DOT_STALE = 0xFF8800; // orange — was connected but timing out
static constexpr uint32_t COLOR_DOT_DOWN = 0xCC3333;  // red — never connected since boot
static constexpr uint32_t COLOR_USB_OFF = 0x444444;   // gray — host not active
static constexpr uint32_t COLOR_LABEL = 0xCCCCCC;
static constexpr uint32_t COLOR_MUTED = 0x666666;

// Warning badge palette + text. Index-aligned with TopBar::WarningKind.
struct WarningSpec {
    const char *text;
    uint32_t bgColor;
    uint32_t fgColor;
};
static constexpr WarningSpec WARNING_SPECS[TopBar::WarningKindCount] = {
    {"NO SD", 0xCC8800, 0xFFFFFF},  // amber — actionable, just insert one
    {"SD ERR", 0xCC3333, 0xFFFFFF}, // red — wiring/firmware issue
};

// Gap between the bar's right edge and the rightmost warning badge.
static constexpr int16_t WARNING_CLUSTER_RIGHT_PAD = 0;
// Gap between adjacent warning badges inside the cluster.
static constexpr int16_t WARNING_BADGE_GAP = 4;
// Inner padding of each badge label.
static constexpr int16_t WARNING_BADGE_PAD_X = 4;
static constexpr int16_t WARNING_BADGE_PAD_Y = 1;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_obj_t *makeStatusDot(lv_obj_t *parent) {
    lv_obj_t *dot = lv_obj_create(parent);
    const int16_t diameter = derivedDot(s_height);
    lv_obj_set_size(dot, diameter, diameter);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
    // Initial colour = red ("never connected"). update() promotes to orange/green
    // once a frame is observed, then back to orange if it later goes stale.
    lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_DOT_DOWN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    return dot;
}

static lv_obj_t *makeBarLabel(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    // Font size is derived from the bar height via the shared proportion table.
    // FontManager::get() snaps to the nearest compiled-in Montserrat size.
    const uint8_t fs = static_cast<uint8_t>(derivedFontSize(s_height));
    lv_obj_set_style_text_font(lbl, FontManager::get(fs), 0);
    return lbl;
}

// Vertical separator glyph "|" — the visible glyph height is roughly the cap
// height of the surrounding font, so we pick the smallest font that yields a
// glyph at least `target` tall. FontManager handles the size→pointer snap.
static lv_obj_t *makeBarSeparator(lv_obj_t *parent, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "|");
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    const uint8_t target = static_cast<uint8_t>(derivedSeparator(s_height));
    lv_obj_set_style_text_font(lbl, FontManager::get(target), 0);
    return lbl;
}

// Build (once) the empty warnings cluster anchored to the right edge of the
// bar. Stays hidden until the first setWarning(kind, true) call. Re-uses the
// bar's pad_all so badges align vertically with other items.
static void buildWarningsCluster() {
    if (s_bar == nullptr)
        return;
    s_warningsCluster = lv_obj_create(s_bar);
    // Sized just-large-enough to hold the badges; lv_obj_set_content_width()
    // would let LVGL size around children, but setting an explicit size and
    // re-aligning children manually is simpler and avoids layout reflows.
    // Height tracks the bar's content area (height minus a small inset, which
    // is small relative to the bar so we keep the legacy 4 px allowance).
    const int16_t clusterH = static_cast<int16_t>(s_height > 4 ? s_height - 4 : s_height);
    lv_obj_set_size(s_warningsCluster, 0, clusterH);
    lv_obj_set_style_bg_opa(s_warningsCluster, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_warningsCluster, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_warningsCluster, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_warningsCluster, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_warningsCluster, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_warningsCluster, LV_ALIGN_RIGHT_MID, -WARNING_CLUSTER_RIGHT_PAD, 0);
    lv_obj_add_flag(s_warningsCluster, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t i = 0; i < TopBar::WarningKindCount; ++i) {
        s_warnings[i].obj = nullptr;
        s_warnings[i].active = false;
    }
}

// Re-pack all active warning badges right-to-left inside the cluster and
// resize the cluster to fit. Called after every setWarning() so the layout
// stays compact when warnings come and go.
static void relayoutWarnings() {
    if (s_warningsCluster == nullptr)
        return;

    int16_t totalWidth = 0;
    uint8_t activeCount = 0;
    lv_obj_t *prev = nullptr;
    for (uint8_t i = 0; i < TopBar::WarningKindCount; ++i) {
        if (!s_warnings[i].active || s_warnings[i].obj == nullptr)
            continue;
        lv_obj_update_layout(s_warnings[i].obj);
        const lv_coord_t w = lv_obj_get_width(s_warnings[i].obj);
        if (prev == nullptr) {
            lv_obj_align(s_warnings[i].obj, LV_ALIGN_RIGHT_MID, 0, 0);
            totalWidth = w;
        } else {
            lv_obj_align_to(s_warnings[i].obj, prev, LV_ALIGN_OUT_LEFT_MID, -WARNING_BADGE_GAP, 0);
            totalWidth += WARNING_BADGE_GAP + w;
        }
        prev = s_warnings[i].obj;
        ++activeCount;
    }

    if (activeCount == 0) {
        lv_obj_add_flag(s_warningsCluster, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(s_warningsCluster, 0);
        return;
    }
    lv_obj_clear_flag(s_warningsCluster, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(s_warningsCluster, totalWidth);
    lv_obj_align(s_warningsCluster, LV_ALIGN_RIGHT_MID, -WARNING_CLUSTER_RIGHT_PAD, 0);
}

// True when at least one signal in the store is currently valid. Used by
// `statusDot` items with signal="any" (the legacy "CAN" presence dot).
static bool anySignalValid() {
    return SignalStore::isValid(SignalIds::RPM) ||
           SignalStore::isValid(SignalIds::COOLANT_TEMP_C) ||
           SignalStore::isValid(SignalIds::BATTERY_VOLTS);
}

// ---------------------------------------------------------------------------
// Layout dispatcher (config-driven path)
// ---------------------------------------------------------------------------

namespace {

// Create one LVGL element for a CfgTopBarItem, append it inside the right
// position bucket, and register it in s_dynItems if it needs per-frame
// updates. Items with kind UNKNOWN are silently skipped (already warned at
// parse time).
void buildItem(const CfgTopBarItem &item, lv_obj_t *prevByPos[3], bool hasDayTheme) {
    lv_obj_t *prev = prevByPos[static_cast<uint8_t>(item.position)];
    lv_obj_t *obj = nullptr;

    auto anchor = [&](lv_obj_t *o, int16_t gapAfterPrev) {
        if (prev == nullptr) {
            // First item in this bucket
            switch (item.position) {
                case TopBarItemPos::LEFT:
                    lv_obj_align(o, LV_ALIGN_LEFT_MID, 0, 0);
                    break;
                case TopBarItemPos::CENTER:
                    lv_obj_align(o, LV_ALIGN_CENTER, 0, 0);
                    break;
                case TopBarItemPos::RIGHT:
                    lv_obj_align(o, LV_ALIGN_RIGHT_MID, 0, 0);
                    break;
            }
        } else if (item.position == TopBarItemPos::RIGHT) {
            lv_obj_align_to(o, prev, LV_ALIGN_OUT_LEFT_MID, -gapAfterPrev, 0);
        } else {
            lv_obj_align_to(o, prev, LV_ALIGN_OUT_RIGHT_MID, gapAfterPrev, 0);
        }
    };

    // Inter-item gap is height-derived (matches Studio's TopBarMetrics.gapRatio).
    const int16_t gap = derivedGap(s_height);

    switch (item.kind) {
        case TopBarItemKind::STATUS_DOT: {
            obj = makeStatusDot(s_bar);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::LABEL: {
            obj = makeBarLabel(s_bar, item.text, COLOR_LABEL);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::SEPARATOR: {
            obj = makeBarSeparator(s_bar, COLOR_MUTED);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::SIGNAL: {
            obj = makeBarLabel(s_bar, "--.-", COLOR_LABEL);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::USB_ICON: {
            obj = lv_label_create(s_bar);
            lv_label_set_text(obj, LV_SYMBOL_DOWNLOAD);
            lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_USB_OFF), 0);
            // Icon size is iconSizeRatio × font size (see proportion table).
            const uint8_t iconSize =
                static_cast<uint8_t>(derivedIconSize(derivedFontSize(s_height)));
            lv_obj_set_style_text_font(obj, FontManager::get(iconSize), 0);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::THEME_TOGGLE: {
            obj = lv_img_create(s_bar);
            lv_img_set_src(obj, ThemeManager::isDayMode() ? "S:/assets/icon_day.bin"
                                                          : "S:/assets/icon_night.bin");
            anchor(obj, 0);
            if (hasDayTheme) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
                // Icon is ~12 px on a 16 px bar — too small to tap reliably.
                // Extend the hit-test region by 10 px on every side (#93).
                lv_obj_set_ext_click_area(obj, 10);
                lv_obj_add_event_cb(
                    obj, [](lv_event_t * /*e*/) { ThemeManager::toggleDayMode(); },
                    LV_EVENT_CLICKED, nullptr);
            } else {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
            // s_themeIcon is the only legacy handle reused — needed by reapplyTheme()
            s_themeIcon = obj;
            break;
        }
        case TopBarItemKind::UNKNOWN:
        default:
            return;
    }

    prevByPos[static_cast<uint8_t>(item.position)] = obj;

    // Track dynamic items for update()
    bool needsUpdate =
        (item.kind == TopBarItemKind::STATUS_DOT || item.kind == TopBarItemKind::SIGNAL ||
         item.kind == TopBarItemKind::USB_ICON);
    if (needsUpdate && s_dynCount < CFG_MAX_TOPBAR_ITEMS) {
        DynItem &d = s_dynItems[s_dynCount++];
        d.kind = item.kind;
        d.obj = obj;
        strlcpy(d.signalId, item.signalId, sizeof(d.signalId));
        strlcpy(d.format, item.format, sizeof(d.format));
        d.lastText[0] = '\0';
        d.lastColor = 0; // 0 = unset sentinel (matches no real color emit below)
        d.lastSeenValid = false;
    }
}

void buildFromLayout(const CfgTopBar &cfg, bool hasDayTheme) {
    s_dynCount = 0;
    lv_obj_t *prevByPos[3] = {nullptr, nullptr, nullptr};
    for (uint8_t i = 0; i < cfg.itemCount; ++i) {
        buildItem(cfg.items[i], prevByPos, hasDayTheme);
    }
}

void buildLegacyHardcoded(const CfgDashboard &dash) {
    const CfgTopBar &cfg = dash.topBar;
    (void)cfg; // colors already applied on s_bar

    // All gaps and font sizes derive from the bar height via the shared
    // proportion table (canshift-core/src/topbar-metrics.ts).
    const int16_t gap = derivedGap(s_height);
    const int16_t fontSize = derivedFontSize(s_height);
    const int16_t iconSize = derivedIconSize(fontSize);
    // Right-side theme-icon offset: roughly the icon glyph width + a gap.
    const int16_t themeIconWidth = static_cast<int16_t>(iconSize + gap);

    // ---- Left: [• dot] ECU [|] CAN [• dot] — matches studio preview ----
    s_ecuDot = makeStatusDot(s_bar);
    lv_obj_align(s_ecuDot, LV_ALIGN_LEFT_MID, 0, 0);

    s_ecuLabel = makeBarLabel(s_bar, "ECU", COLOR_LABEL);
    lv_obj_align_to(s_ecuLabel, s_ecuDot, LV_ALIGN_OUT_RIGHT_MID, gap, 0);

    lv_obj_t *leftSep = makeBarSeparator(s_bar, COLOR_MUTED);
    lv_obj_align_to(leftSep, s_ecuLabel, LV_ALIGN_OUT_RIGHT_MID, gap, 0);

    s_canLabel = makeBarLabel(s_bar, "CAN", COLOR_LABEL);
    lv_obj_align_to(s_canLabel, leftSep, LV_ALIGN_OUT_RIGHT_MID, gap, 0);

    s_canDot = makeStatusDot(s_bar);
    lv_obj_align_to(s_canDot, s_canLabel, LV_ALIGN_OUT_RIGHT_MID, gap, 0);

    // ---- Right cluster (built right-to-left): theme icon, |, USB icon, voltage ----
    // Theme toggle — image asset (Montserrat compile-time fonts have no sun/moon
    // glyphs). Use lv_img directly with a CLICKABLE flag — lv_imgbtn would need
    // 3-state sources (released/pressed/checked) which we don't have.
    s_themeIcon = lv_img_create(s_bar);
    lv_img_set_src(s_themeIcon, ThemeManager::isDayMode() ? "S:/assets/icon_day.bin"
                                                          : "S:/assets/icon_night.bin");
    lv_obj_align(s_themeIcon, LV_ALIGN_RIGHT_MID, 0, 0);
    if (dash.hasDayTheme) {
        lv_obj_add_flag(s_themeIcon, LV_OBJ_FLAG_CLICKABLE);
        // See layout-driven path: extend hit-test by 10 px (#93).
        lv_obj_set_ext_click_area(s_themeIcon, 10);
        lv_obj_add_event_cb(
            s_themeIcon, [](lv_event_t * /*e*/) { ThemeManager::toggleDayMode(); },
            LV_EVENT_CLICKED, nullptr);
    } else {
        lv_obj_add_flag(s_themeIcon, LV_OBJ_FLAG_HIDDEN);
    }

    // Separator between USB icon and theme icon — anchored to right edge with
    // a gap accounting for the theme icon width.
    lv_obj_t *rightSep = makeBarSeparator(s_bar, COLOR_MUTED);
    lv_obj_align(rightSep, LV_ALIGN_RIGHT_MID, -themeIconWidth, 0);

    // USB / download icon — left of the separator
    s_usbIcon = lv_label_create(s_bar);
    lv_label_set_text(s_usbIcon, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_color(s_usbIcon, lv_color_hex(COLOR_USB_OFF), 0);
    lv_obj_set_style_text_font(s_usbIcon, FontManager::get(static_cast<uint8_t>(iconSize)), 0);
    lv_obj_align_to(s_usbIcon, rightSep, LV_ALIGN_OUT_LEFT_MID, -gap, 0);

    // Voltage — left of the USB icon
    s_voltageLabel = makeBarLabel(s_bar, "--.-V", COLOR_LABEL);
    lv_obj_align_to(s_voltageLabel, s_usbIcon, LV_ALIGN_OUT_LEFT_MID, -gap, 0);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TopBar::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    const CfgTopBar &cfg = dash.topBar;
    s_height = cfg.height > 0 ? cfg.height : 30;

    s_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bar, LV_HOR_RES, s_height);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(cfg.bgColor.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_MAIN);
    // Horizontal padding follows the shared proportion table (paddingRatio in
    // canshift-core/src/topbar-metrics.ts); vertical padding stays 0 so child
    // alignments resolve cleanly inside the full bar height. Studio applies
    // the same padding via `padding: 0 px` on its bar container.
    lv_obj_set_style_pad_hor(s_bar, derivedPadding(s_height), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    // Bar itself is clickable — tapping a non-widget area while Settings is
    // open closes the panel (alongside the swipe-up gesture).
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        s_bar,
        [](lv_event_t *e) {
            if (lv_event_get_target(e) != lv_event_get_current_target(e))
                return;
            if (!SettingsPage::isOpen())
                return;
            // A tap on the top bar can be flagged by LVGL as a tiny down-swipe,
            // which opens Settings, immediately followed by the click event
            // that would close it again — net effect is a flicker. Skip the
            // close if Settings was opened in the last ~300 ms (i.e. opened
            // by the same touch event).
            if (millis() - SettingsPage::lastOpenMs() < 300)
                return;
            LOG_INFO("UI", "Top bar tapped — closing Settings");
            SettingsPage::close();
        },
        LV_EVENT_CLICKED, nullptr);

    // Reset all handle state — needed when init() runs a second time (it
    // currently doesn't, but cheap insurance).
    s_ecuDot = s_ecuLabel = s_canDot = s_canLabel = nullptr;
    s_voltageLabel = s_usbIcon = s_themeIcon = nullptr;
    s_warningsCluster = nullptr;
    s_dynCount = 0;
    for (uint8_t i = 0; i < TopBar::WarningKindCount; ++i) {
        s_warnings[i].obj = nullptr;
        s_warnings[i].active = false;
    }

    if (cfg.hasLayout) {
        buildFromLayout(cfg, dash.hasDayTheme);
    } else {
        buildLegacyHardcoded(dash);
    }

    // Build the warnings cluster last so it draws on top of any configured
    // right-aligned items. Empty + hidden by default; setWarning() reveals it.
    buildWarningsCluster();

    SettingsPage::init(s_height, static_cast<int16_t>(LV_VER_RES - s_height));

    LOG_INFO("UI", "Top bar initialized (height=%dpx, layout=%s, dyn=%u)", s_height,
             cfg.hasLayout ? "config" : "legacy", static_cast<unsigned>(s_dynCount));
}

void TopBar::reapplyTheme() {
    if (!s_bar)
        return;
    const CfgTopBar &cfg = ConfigLoader::getDashboardConfig().topBar;
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(cfg.bgColor.rgb), LV_PART_MAIN);
    if (s_themeIcon) {
        lv_img_set_src(s_themeIcon, ThemeManager::isDayMode() ? "S:/assets/icon_day.bin"
                                                              : "S:/assets/icon_night.bin");
    }
}

// Pick the bg color for a status dot based on signal freshness, with
// "stale" (orange) once it's been seen at least once and "down" (red)
// before any sample has arrived this boot.
static uint32_t statusDotColor(bool valid, bool everSeen) {
    if (valid)
        return COLOR_DOT_OK;
    return everSeen ? COLOR_DOT_STALE : COLOR_DOT_DOWN;
}

// Per-DynItem "ever seen valid" history — index-aligned with s_dynItems.
static bool s_dynEverSeen[CFG_MAX_TOPBAR_ITEMS] = {};

static void updateDynStatusDot(uint8_t idx, DynItem &d) {
    bool valid = false;
    if (d.signalId[0] == '\0' || strcmp(d.signalId, "any") == 0) {
        valid = anySignalValid();
    } else {
        SignalId sid = signalIdFromName(d.signalId);
        valid = (sid < SignalIds::SIGNAL_COUNT) && SignalStore::isValid(sid);
    }
    if (valid)
        s_dynEverSeen[idx] = true;
    const uint32_t color = statusDotColor(valid, s_dynEverSeen[idx]);
    if (color == d.lastColor)
        return; // skip lvgl style write — no change
    lv_obj_set_style_bg_color(d.obj, lv_color_hex(color), LV_PART_MAIN);
    d.lastColor = color;
}

static void updateDynSignalLabel(DynItem &d) {
    SignalId sid = signalIdFromName(d.signalId);
    const char *fmt = d.format[0] ? d.format : "%.1f";
    char buf[16];
    uint32_t targetColor;
    if (sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid)) {
        const float v = SignalStore::read(sid, 0.0f);
        FloatFormat::formatFromSpec(buf, sizeof(buf), v, fmt);
        targetColor = COLOR_LABEL;
    } else {
        strlcpy(buf, "--.-", sizeof(buf));
        targetColor = COLOR_MUTED;
    }
    if (strcmp(buf, d.lastText) != 0) {
        lv_label_set_text(d.obj, buf);
        strlcpy(d.lastText, buf, sizeof(d.lastText));
    }
    if (targetColor != d.lastColor) {
        lv_obj_set_style_text_color(d.obj, lv_color_hex(targetColor), 0);
        d.lastColor = targetColor;
    }
}

static void updateUsbIcon(lv_obj_t *obj, DynItem *d) {
    const bool active = UsbComm::isHostActive();
    const uint32_t color = active ? COLOR_DOT_OK : COLOR_USB_OFF;
    if (d != nullptr && color == d->lastColor)
        return;
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (d != nullptr)
        d->lastColor = color;
}

void TopBar::update() {
    if (!s_bar)
        return;

    // ---- Layout-driven path ----
    if (s_dynCount > 0) {
        for (uint8_t i = 0; i < s_dynCount; ++i) {
            DynItem &d = s_dynItems[i];
            switch (d.kind) {
                case TopBarItemKind::STATUS_DOT:
                    updateDynStatusDot(i, d);
                    break;
                case TopBarItemKind::SIGNAL:
                    updateDynSignalLabel(d);
                    break;
                case TopBarItemKind::USB_ICON:
                    updateUsbIcon(d.obj, &d);
                    break;
                default:
                    break;
            }
        }
        return;
    }

    // ---- Legacy hardcoded path ----
    // Cached last-rendered state — skip LVGL style/text writes when nothing
    // changed (issue #95, fix F3). 0 sentinel = first run, forces an emit.
    static bool s_ecuEverSeen = false;
    static bool s_canEverSeen = false;
    static uint32_t s_ecuDotColorLast = 0;
    static uint32_t s_canDotColorLast = 0;
    static char s_voltageTextLast[8] = {};
    static uint32_t s_voltageColorLast = 0;
    static uint32_t s_usbColorLast = 0;

    const bool ecuValid = SignalStore::isValid(SignalIds::RPM);
    const bool canValid = ecuValid || anySignalValid();
    if (ecuValid)
        s_ecuEverSeen = true;
    if (canValid)
        s_canEverSeen = true;

    if (s_ecuDot) {
        const uint32_t c = statusDotColor(ecuValid, s_ecuEverSeen);
        if (c != s_ecuDotColorLast) {
            lv_obj_set_style_bg_color(s_ecuDot, lv_color_hex(c), LV_PART_MAIN);
            s_ecuDotColorLast = c;
        }
    }
    if (s_canDot) {
        const uint32_t c = statusDotColor(canValid, s_canEverSeen);
        if (c != s_canDotColorLast) {
            lv_obj_set_style_bg_color(s_canDot, lv_color_hex(c), LV_PART_MAIN);
            s_canDotColorLast = c;
        }
    }
    if (s_voltageLabel) {
        char buf[8];
        uint32_t targetColor;
        if (SignalStore::isValid(SignalIds::BATTERY_VOLTS)) {
            const float v = SignalStore::read(SignalIds::BATTERY_VOLTS, 0.0f);
            FloatFormat::formatFromSpec(buf, sizeof(buf), v, "%.1fV");
            targetColor = COLOR_LABEL;
        } else {
            strlcpy(buf, "--.-V", sizeof(buf));
            targetColor = COLOR_MUTED;
        }
        if (strcmp(buf, s_voltageTextLast) != 0) {
            lv_label_set_text(s_voltageLabel, buf);
            strlcpy(s_voltageTextLast, buf, sizeof(s_voltageTextLast));
        }
        if (targetColor != s_voltageColorLast) {
            lv_obj_set_style_text_color(s_voltageLabel, lv_color_hex(targetColor), 0);
            s_voltageColorLast = targetColor;
        }
    }
    if (s_usbIcon) {
        const bool active = UsbComm::isHostActive();
        const uint32_t color = active ? COLOR_DOT_OK : COLOR_USB_OFF;
        if (color != s_usbColorLast) {
            lv_obj_set_style_text_color(s_usbIcon, lv_color_hex(color), 0);
            s_usbColorLast = color;
        }
    }
}

int16_t TopBar::getHeight() {
    return s_height;
}

void TopBar::setWarning(WarningKind kind, bool active) {
    const uint8_t idx = static_cast<uint8_t>(kind);
    if (idx >= TopBar::WarningKindCount)
        return;
    if (s_warningsCluster == nullptr)
        return; // init() not yet run
    if (s_warnings[idx].active == active)
        return; // idempotent

    if (active) {
        const WarningSpec &spec = WARNING_SPECS[idx];
        lv_obj_t *badge = lv_label_create(s_warningsCluster);
        lv_label_set_text(badge, spec.text);
        lv_obj_set_style_text_color(badge, lv_color_hex(spec.fgColor), 0);
        // Badge font follows the shared proportion table — same size as bar labels.
        const uint8_t badgeFs = static_cast<uint8_t>(derivedFontSize(s_height));
        lv_obj_set_style_text_font(badge, FontManager::get(badgeFs), 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(spec.bgColor), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(badge, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(badge, WARNING_BADGE_PAD_X, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(badge, WARNING_BADGE_PAD_Y, LV_PART_MAIN);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        s_warnings[idx].obj = badge;
        s_warnings[idx].active = true;
    } else {
        if (s_warnings[idx].obj != nullptr) {
            lv_obj_del(s_warnings[idx].obj);
            s_warnings[idx].obj = nullptr;
        }
        s_warnings[idx].active = false;
    }

    relayoutWarnings();
}
