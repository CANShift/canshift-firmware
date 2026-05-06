// top_bar.cpp — Persistent top status bar
//
// Layout (left to right):
//   [• ECU]  [• CAN]  ......  PAGE NAME  ......  12.4V  ↓  ☀
//
// Settings is opened by swiping down from the top of the screen — there is no
// dedicated gear button (#50, redundant given the gesture).
//
// Status sources:
//   ECU dot:  green when SignalIds::RPM is valid (recent ECU frame received)
//   CAN dot:  green when at least one signal has been received recently
//   Voltage:  SignalIds::BATTERY_VOLTS — formatted "12.4V" (— if unknown)
//   Page:     PageManager::getCurrentPageId(), uppercased
//   Download: green when UsbComm reports a recent host command (studio attached)

#include "top_bar.h"
#include "ui/font_manager.h"
#include "ui/page_manager.h"
#include "settings_page.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "can/signal_map.h"
#include "hal/usb/usb_comm.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <ctype.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static lv_obj_t *s_bar = nullptr;

// Hardcoded-layout handles. Used when `topBar.layout` is absent (legacy path).
// When the layout is config-driven these stay null and update() walks the
// dynamic-item table instead.
static lv_obj_t *s_ecuDot = nullptr;
static lv_obj_t *s_ecuLabel = nullptr;
static lv_obj_t *s_canDot = nullptr;
static lv_obj_t *s_canLabel = nullptr;

static lv_obj_t *s_pageLabel = nullptr;
static lv_obj_t *s_voltageLabel = nullptr;
static lv_obj_t *s_usbIcon = nullptr;

static lv_obj_t *s_themeIcon = nullptr;

static int16_t s_height = 30;

// Dynamic item table — populated when `topBar.layout` drives the rendering.
// Each entry stores the LVGL handle plus the metadata needed for update().
struct DynItem {
    TopBarItemKind kind;
    lv_obj_t *obj;             // primary lv object (label / dot)
    char signalId[CFG_MAX_SIGNAL_LEN];
    char format[16];
};
static DynItem s_dynItems[CFG_MAX_TOPBAR_ITEMS];
static uint8_t s_dynCount = 0;

// Day/night icons live on the SD as 12×12 RGB565 .bin (LVGL native format).
// Source PNGs are in scripts/icon_sources/, regenerable via
// scripts/png_to_lvgl_bin.py. We can't use the Unicode sun/moon glyphs
// because the compile-time Montserrat fonts don't include them.

static constexpr uint32_t COLOR_DOT_OK    = 0x33CC44; // green — connected, fresh data
static constexpr uint32_t COLOR_DOT_STALE = 0xFF8800; // orange — was connected but timing out
static constexpr uint32_t COLOR_DOT_DOWN  = 0xCC3333; // red — never connected since boot
static constexpr uint32_t COLOR_USB_OFF   = 0x444444; // gray — host not active
static constexpr uint32_t COLOR_LABEL     = 0xCCCCCC;
static constexpr uint32_t COLOR_MUTED     = 0x666666;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static lv_obj_t *makeStatusDot(lv_obj_t *parent) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 8, 8);
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
    lv_obj_set_style_text_font(lbl, FontManager::get(12), 0);
    return lbl;
}

static void uppercaseCopy(char *dst, size_t dstLen, const char *src) {
    if (dstLen == 0) return;
    size_t i = 0;
    for (; src[i] && i + 1 < dstLen; i++) {
        dst[i] = static_cast<char>(toupper(static_cast<unsigned char>(src[i])));
    }
    dst[i] = '\0';
}

// True when at least one signal in the store is currently valid. Used by
// `statusDot` items with signal="any" (the legacy "CAN" presence dot).
static bool anySignalValid() {
    return SignalStore::isValid(SignalIds::RPM)
        || SignalStore::isValid(SignalIds::COOLANT_TEMP_C)
        || SignalStore::isValid(SignalIds::BATTERY_VOLTS);
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

    switch (item.kind) {
        case TopBarItemKind::STATUS_DOT: {
            obj = makeStatusDot(s_bar);
            anchor(obj, 4);
            break;
        }
        case TopBarItemKind::LABEL: {
            obj = makeBarLabel(s_bar, item.text, COLOR_LABEL);
            anchor(obj, 4);
            break;
        }
        case TopBarItemKind::SEPARATOR: {
            obj = makeBarLabel(s_bar, "|", COLOR_MUTED);
            anchor(obj, 6);
            break;
        }
        case TopBarItemKind::PAGE_NAME: {
            obj = makeBarLabel(s_bar, "", COLOR_LABEL);
            anchor(obj, 0);
            break;
        }
        case TopBarItemKind::SIGNAL: {
            obj = makeBarLabel(s_bar, "--.-", COLOR_LABEL);
            anchor(obj, 8);
            break;
        }
        case TopBarItemKind::USB_ICON: {
            obj = lv_label_create(s_bar);
            lv_label_set_text(obj, LV_SYMBOL_DOWNLOAD);
            lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_USB_OFF), 0);
            lv_obj_set_style_text_font(obj, FontManager::get(14), 0);
            anchor(obj, 4);
            break;
        }
        case TopBarItemKind::THEME_TOGGLE: {
            obj = lv_img_create(s_bar);
            lv_img_set_src(obj, ThemeManager::isDayMode() ? "S:/assets/icon_day.bin"
                                                          : "S:/assets/icon_night.bin");
            anchor(obj, 0);
            if (hasDayTheme) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
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
    bool needsUpdate = (item.kind == TopBarItemKind::STATUS_DOT ||
                        item.kind == TopBarItemKind::PAGE_NAME ||
                        item.kind == TopBarItemKind::SIGNAL ||
                        item.kind == TopBarItemKind::USB_ICON);
    if (needsUpdate && s_dynCount < CFG_MAX_TOPBAR_ITEMS) {
        DynItem &d = s_dynItems[s_dynCount++];
        d.kind = item.kind;
        d.obj = obj;
        strlcpy(d.signalId, item.signalId, sizeof(d.signalId));
        strlcpy(d.format, item.format, sizeof(d.format));
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

    // ---- Left: [• dot] ECU [|] CAN [• dot] — matches studio preview ----
    s_ecuDot = makeStatusDot(s_bar);
    lv_obj_align(s_ecuDot, LV_ALIGN_LEFT_MID, 0, 0);

    s_ecuLabel = makeBarLabel(s_bar, "ECU", COLOR_LABEL);
    lv_obj_align_to(s_ecuLabel, s_ecuDot, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    lv_obj_t *leftSep = makeBarLabel(s_bar, "|", COLOR_MUTED);
    lv_obj_align_to(leftSep, s_ecuLabel, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    s_canLabel = makeBarLabel(s_bar, "CAN", COLOR_LABEL);
    lv_obj_align_to(s_canLabel, leftSep, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    s_canDot = makeStatusDot(s_bar);
    lv_obj_align_to(s_canDot, s_canLabel, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    // ---- Center: current page name ----
    s_pageLabel = makeBarLabel(s_bar, "", COLOR_LABEL);
    lv_obj_align(s_pageLabel, LV_ALIGN_CENTER, 0, 0);

    // ---- Right cluster (built right-to-left): theme icon, |, USB icon, voltage ----
    // Theme toggle — image asset (Montserrat compile-time fonts have no sun/moon
    // glyphs). Use lv_img directly with a CLICKABLE flag — lv_imgbtn would need
    // 3-state sources (released/pressed/checked) which we don't have.
    s_themeIcon = lv_img_create(s_bar);
    lv_img_set_src(s_themeIcon,
                   ThemeManager::isDayMode() ? "S:/assets/icon_day.bin"
                                             : "S:/assets/icon_night.bin");
    lv_obj_align(s_themeIcon, LV_ALIGN_RIGHT_MID, 0, 0);
    if (dash.hasDayTheme) {
        lv_obj_add_flag(s_themeIcon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            s_themeIcon,
            [](lv_event_t * /*e*/) { ThemeManager::toggleDayMode(); },
            LV_EVENT_CLICKED, nullptr);
    } else {
        lv_obj_add_flag(s_themeIcon, LV_OBJ_FLAG_HIDDEN);
    }

    // Separator between USB icon and theme icon — width of theme icon (~12) + gap.
    lv_obj_t *rightSep = makeBarLabel(s_bar, "|", COLOR_MUTED);
    lv_obj_align(rightSep, LV_ALIGN_RIGHT_MID, -16, 0);

    // USB / download icon — left of the separator
    s_usbIcon = lv_label_create(s_bar);
    lv_label_set_text(s_usbIcon, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_color(s_usbIcon, lv_color_hex(COLOR_USB_OFF), 0);
    lv_obj_set_style_text_font(s_usbIcon, FontManager::get(14), 0);
    lv_obj_align_to(s_usbIcon, rightSep, LV_ALIGN_OUT_LEFT_MID, -4, 0);

    // Voltage — left of the USB icon
    s_voltageLabel = makeBarLabel(s_bar, "--.-V", COLOR_LABEL);
    lv_obj_align_to(s_voltageLabel, s_usbIcon, LV_ALIGN_OUT_LEFT_MID, -8, 0);
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
    // pad_all=2 leaves a 12 px content area in a 16 px bar — exactly the size
    // of the day/night icons. Larger padding clipped them.
    lv_obj_set_style_pad_all(s_bar, 2, LV_PART_MAIN);
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
            if (millis() - SettingsPage::lastOpenMs() < 300) return;
            LOG_INFO("UI", "Top bar tapped — closing Settings");
            SettingsPage::close();
        },
        LV_EVENT_CLICKED, nullptr);

    // Reset all handle state — needed when init() runs a second time (it
    // currently doesn't, but cheap insurance).
    s_ecuDot = s_ecuLabel = s_canDot = s_canLabel = nullptr;
    s_pageLabel = s_voltageLabel = s_usbIcon = s_themeIcon = nullptr;
    s_dynCount = 0;

    if (cfg.hasLayout) {
        buildFromLayout(cfg, dash.hasDayTheme);
    } else {
        buildLegacyHardcoded(dash);
    }

    SettingsPage::init(s_height, static_cast<int16_t>(LV_VER_RES - s_height));

    LOG_INFO("UI", "Top bar initialized (height=%dpx, layout=%s, dyn=%u)", s_height,
             cfg.hasLayout ? "config" : "legacy", static_cast<unsigned>(s_dynCount));
}

void TopBar::reapplyTheme() {
    if (!s_bar) return;
    const CfgTopBar &cfg = ConfigLoader::getDashboardConfig().topBar;
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(cfg.bgColor.rgb), LV_PART_MAIN);
    if (s_themeIcon) {
        lv_img_set_src(s_themeIcon,
                       ThemeManager::isDayMode() ? "S:/assets/icon_day.bin"
                                                 : "S:/assets/icon_night.bin");
    }
}

// Pick the bg color for a status dot based on signal freshness, with
// "stale" (orange) once it's been seen at least once and "down" (red)
// before any sample has arrived this boot.
static uint32_t statusDotColor(bool valid, bool everSeen) {
    if (valid) return COLOR_DOT_OK;
    return everSeen ? COLOR_DOT_STALE : COLOR_DOT_DOWN;
}

// Per-DynItem "ever seen valid" history — index-aligned with s_dynItems.
static bool s_dynEverSeen[CFG_MAX_TOPBAR_ITEMS] = {};

static void updateDynStatusDot(uint8_t idx, const DynItem &d) {
    bool valid = false;
    if (d.signalId[0] == '\0' || strcmp(d.signalId, "any") == 0) {
        valid = anySignalValid();
    } else {
        SignalId sid = signalIdFromName(d.signalId);
        valid = (sid < SignalIds::SIGNAL_COUNT) && SignalStore::isValid(sid);
    }
    if (valid) s_dynEverSeen[idx] = true;
    lv_obj_set_style_bg_color(d.obj,
                              lv_color_hex(statusDotColor(valid, s_dynEverSeen[idx])),
                              LV_PART_MAIN);
}

static void updateDynSignalLabel(const DynItem &d) {
    SignalId sid = signalIdFromName(d.signalId);
    const char *fmt = d.format[0] ? d.format : "%.1f";
    if (sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid)) {
        float v = SignalStore::read(sid, 0.0f);
        char buf[16];
        snprintf(buf, sizeof(buf), fmt, v);
        lv_label_set_text(d.obj, buf);
        lv_obj_set_style_text_color(d.obj, lv_color_hex(COLOR_LABEL), 0);
    } else {
        lv_label_set_text(d.obj, "--.-");
        lv_obj_set_style_text_color(d.obj, lv_color_hex(COLOR_MUTED), 0);
    }
}

static void updatePageNameLabel(lv_obj_t *obj) {
    const char *pageId = PageManager::getCurrentPageId();
    if (!pageId) return;
    char buf[16];
    uppercaseCopy(buf, sizeof(buf), pageId);
    lv_label_set_text(obj, buf);
}

static void updateUsbIcon(lv_obj_t *obj) {
    const bool active = UsbComm::isHostActive();
    lv_obj_set_style_text_color(obj,
                                lv_color_hex(active ? COLOR_DOT_OK : COLOR_USB_OFF), 0);
}

void TopBar::update() {
    if (!s_bar) return;

    // ---- Layout-driven path ----
    if (s_dynCount > 0) {
        for (uint8_t i = 0; i < s_dynCount; ++i) {
            const DynItem &d = s_dynItems[i];
            switch (d.kind) {
                case TopBarItemKind::STATUS_DOT: updateDynStatusDot(i, d); break;
                case TopBarItemKind::SIGNAL:     updateDynSignalLabel(d); break;
                case TopBarItemKind::PAGE_NAME:  updatePageNameLabel(d.obj); break;
                case TopBarItemKind::USB_ICON:   updateUsbIcon(d.obj); break;
                default: break;
            }
        }
        return;
    }

    // ---- Legacy hardcoded path ----
    static bool s_ecuEverSeen = false;
    static bool s_canEverSeen = false;

    const bool ecuValid = SignalStore::isValid(SignalIds::RPM);
    const bool canValid = ecuValid || anySignalValid();
    if (ecuValid) s_ecuEverSeen = true;
    if (canValid) s_canEverSeen = true;

    if (s_ecuDot) {
        lv_obj_set_style_bg_color(s_ecuDot,
                                  lv_color_hex(statusDotColor(ecuValid, s_ecuEverSeen)),
                                  LV_PART_MAIN);
    }
    if (s_canDot) {
        lv_obj_set_style_bg_color(s_canDot,
                                  lv_color_hex(statusDotColor(canValid, s_canEverSeen)),
                                  LV_PART_MAIN);
    }
    if (s_pageLabel) updatePageNameLabel(s_pageLabel);

    if (s_voltageLabel) {
        if (SignalStore::isValid(SignalIds::BATTERY_VOLTS)) {
            float v = SignalStore::read(SignalIds::BATTERY_VOLTS, 0.0f);
            char buf[8];
            snprintf(buf, sizeof(buf), "%.1fV", v);
            lv_label_set_text(s_voltageLabel, buf);
            lv_obj_set_style_text_color(s_voltageLabel, lv_color_hex(COLOR_LABEL), 0);
        } else {
            lv_label_set_text(s_voltageLabel, "--.-V");
            lv_obj_set_style_text_color(s_voltageLabel, lv_color_hex(COLOR_MUTED), 0);
        }
    }
    if (s_usbIcon) updateUsbIcon(s_usbIcon);
}

int16_t TopBar::getHeight() {
    return s_height;
}
