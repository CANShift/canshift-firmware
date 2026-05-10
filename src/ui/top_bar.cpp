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

// Theme toggle handle — captured during layout build so reapplyTheme() can
// swap the icon source without rebuilding the bar.
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

// Day/night icons live on SPIFFS as 12×12 RGB565 .bin (LVGL native format).
// Source PNGs are in scripts/icon_sources/, regenerable via
// scripts/png_to_lvgl_bin.py. We can't use the Unicode sun/moon glyphs
// because the Latin-only Orbitron fonts don't include them.

static constexpr uint32_t COLOR_DOT_OK = 0x33CC44;    // green — connected, fresh data
static constexpr uint32_t COLOR_DOT_STALE = 0xFF8800; // orange — was connected but timing out
static constexpr uint32_t COLOR_DOT_DOWN = 0xCC3333;  // red — never connected since boot
static constexpr uint32_t COLOR_USB_OFF = 0x444444;   // gray — host not active
static constexpr uint32_t COLOR_LABEL = 0xCCCCCC;
static constexpr uint32_t COLOR_MUTED = 0x666666;

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
    // FontManager::label() snaps to the nearest cached Orbitron Medium size.
    const uint8_t fs = static_cast<uint8_t>(derivedFontSize(s_height));
    lv_obj_set_style_text_font(lbl, FontManager::label(fs), 0);
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
    lv_obj_set_style_text_font(lbl, FontManager::label(target), 0);
    return lbl;
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
            lv_obj_set_style_text_font(obj, FontManager::label(iconSize), 0);
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
            // Captured so reapplyTheme() can swap the icon when the mode flips.
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

    // Pass 1: left + center items in array order.
    for (uint8_t i = 0; i < cfg.itemCount; ++i) {
        const auto pos = cfg.items[i].position;
        if (pos == TopBarItemPos::LEFT || pos == TopBarItemPos::CENTER) {
            buildItem(cfg.items[i], prevByPos, hasDayTheme);
        }
    }

    // Pass 2: right items in REVERSE — last right item anchors to
    // RIGHT_MID; earlier ones grow leftward via OUT_LEFT_MID. Matches
    // studio's flex-row order and the legacy hardcoded path. Fixes #480.
    for (int i = cfg.itemCount - 1; i >= 0; --i) {
        if (cfg.items[i].position == TopBarItemPos::RIGHT) {
            buildItem(cfg.items[i], prevByPos, hasDayTheme);
        }
    }
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

    // Reset captured handles — cheap insurance against re-init.
    s_themeIcon = nullptr;
    s_dynCount = 0;

    buildFromLayout(cfg, dash.hasDayTheme);

    SettingsPage::init(s_height, static_cast<int16_t>(LV_VER_RES - s_height));

    LOG_INFO("UI", "Top bar initialized (height=%dpx, dyn=%u)", s_height,
             static_cast<unsigned>(s_dynCount));
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
}

int16_t TopBar::getHeight() {
    return s_height;
}
