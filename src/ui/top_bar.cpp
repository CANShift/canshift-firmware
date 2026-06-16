#include "top_bar.h"
#include "app_config.h"
#include "icon_assets.h"
#include "ui/font_manager.h"
#include "settings_page.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "runtime/signal_store.h"
#include "runtime/track_store.h"
#include "can/can_manager.h"
#include "can/signal_map.h"
#include "hal/usb/usb_comm.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif
#include "diag/logger.h"
#include "util/format_float.h"

#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace TopBarMetrics {
constexpr float DOT_RATIO = 0.30f;
constexpr float FONT_SIZE_RATIO = 0.45f;
constexpr float SEPARATOR_RATIO = 0.55f;
constexpr float GAP_RATIO = 0.25f;
constexpr float PADDING_RATIO = 0.40f;
constexpr float ICON_SIZE_RATIO = 1.15f;
} // namespace TopBarMetrics

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

static lv_obj_t *s_bar = nullptr;
static lv_obj_t *s_themeIcon = nullptr;
static int16_t s_height = 30;

struct DynItem {
    TopBarItemKind kind;
    lv_obj_t *obj;
    char signalId[CFG_MAX_SIGNAL_LEN];
    char format[16];

    char lastText[16];
    uint32_t lastColor;
    bool lastSeenValid;

    bool hidden;
    int8_t linkedFlagIdx;
    int8_t nextFlagIdx;
};
static DynItem s_dynItems[CFG_MAX_TOPBAR_ITEMS];
static uint8_t s_dynCount = 0;

static bool s_dynEverSeen[CFG_MAX_TOPBAR_ITEMS] = {};

static constexpr uint32_t COLOR_DOT_OK = 0x33CC44;
static constexpr uint32_t COLOR_DOT_STALE = 0xFF8800;
static constexpr uint32_t COLOR_DOT_DOWN = 0xCC3333;
static constexpr uint32_t COLOR_BLE_CONN = 0x4499FF;
static constexpr uint32_t COLOR_BLE_ADV = 0x225588;
static constexpr uint32_t COLOR_BLE_OFF = 0x444444;
static constexpr uint32_t COLOR_MODE_ACTIVE = 0xFF8800;
static constexpr uint32_t COLOR_MODE_IDLE = 0x1C1C1C;
static constexpr uint32_t COLOR_LABEL = 0xCCCCCC;
static constexpr uint32_t COLOR_MUTED = 0x666666;

static lv_obj_t *makeStatusDot(lv_obj_t *parent) {
    lv_obj_t *dot = lv_obj_create(parent);
    const int16_t diameter = derivedDot(s_height);
    lv_obj_set_size(dot, diameter, diameter);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
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

    (void)derivedFontSize;
    lv_obj_set_style_text_font(lbl, FontManager::label(12), 0);
    return lbl;
}

static lv_obj_t *makeBarSeparator(lv_obj_t *parent, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "|");
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    const uint8_t target = static_cast<uint8_t>(derivedSeparator(s_height));
    lv_obj_set_style_text_font(lbl, FontManager::label(target), 0);
    return lbl;
}

static constexpr uint32_t CAN_BUS_LIVE_THRESHOLD_MS = 2000;

static bool anySignalValid() {
    static constexpr SignalId kAnyIds[] = {SignalIds::RPM, SignalIds::COOLANT_TEMP_C,
                                           SignalIds::BATTERY_VOLTS};
    if (CanManager::msSinceLastRx() > CAN_BUS_LIVE_THRESHOLD_MS)
        return false;
    return SignalStore::anyValid(kAnyIds, sizeof(kAnyIds) / sizeof(kAnyIds[0]));
}

namespace {

void buildItem(const CfgTopBarItem &item, lv_obj_t *prevByPos[3], int8_t lastModeFlagIdxByPos[3],
               int8_t pendingSepNeedingNextByPos[3], bool hasDayTheme) {
    lv_obj_t *prev = prevByPos[static_cast<uint8_t>(item.position)];
    lv_obj_t *obj = nullptr;

    auto anchor = [&](lv_obj_t *o, int16_t gapAfterPrev) {
        if (prev == nullptr) {
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

            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::SIGNAL: {

            obj = makeBarLabel(s_bar, "", COLOR_LABEL);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::BLE_ICON: {
            obj = lv_label_create(s_bar);
            lv_label_set_text(obj, "BLE");
            lv_obj_set_style_text_color(obj, lv_color_hex(COLOR_BLE_OFF), 0);
            lv_obj_set_style_text_font(obj, FontManager::label(12), 0);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::MODE_FLAG: {
            obj = makeBarLabel(s_bar, item.text, COLOR_MODE_ACTIVE);

            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::TRACK_BADGE: {
            obj = makeBarLabel(s_bar, "TRACK", COLOR_MODE_ACTIVE);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::THEME_TOGGLE: {
            obj = lv_img_create(s_bar);
            lv_img_set_src(obj, IconAssets::resolveSource(
                                    ThemeManager::isDayMode() ? "icon_day" : "icon_night"));
            anchor(obj, 0);
            if (hasDayTheme) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);

                lv_obj_set_ext_click_area(obj, 10);
                lv_obj_add_event_cb(
                    obj, [](lv_event_t *) { ThemeManager::toggleDayMode(); }, LV_EVENT_CLICKED,
                    nullptr);
            } else {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
            s_themeIcon = obj;
            break;
        }
        case TopBarItemKind::UNKNOWN:
        default:
            return;
    }

    prevByPos[static_cast<uint8_t>(item.position)] = obj;

    const uint8_t posIdx = static_cast<uint8_t>(item.position);
    const int8_t prevFlagIdx = lastModeFlagIdxByPos[posIdx];
    bool needsUpdate =
        (item.kind == TopBarItemKind::STATUS_DOT || item.kind == TopBarItemKind::SIGNAL ||
         item.kind == TopBarItemKind::BLE_ICON || item.kind == TopBarItemKind::MODE_FLAG ||
         item.kind == TopBarItemKind::TRACK_BADGE ||
         (item.kind == TopBarItemKind::SEPARATOR && prevFlagIdx >= 0));
    if (needsUpdate && s_dynCount < CFG_MAX_TOPBAR_ITEMS) {
        const uint8_t myIdx = s_dynCount;
        DynItem &d = s_dynItems[s_dynCount++];
        d.kind = item.kind;
        d.obj = obj;
        strlcpy(d.signalId, item.signalId, sizeof(d.signalId));
        strlcpy(d.format, item.format, sizeof(d.format));
        d.lastText[0] = '\0';
        d.lastColor = 0;
        d.lastSeenValid = false;

        d.hidden =
            (item.kind == TopBarItemKind::MODE_FLAG || item.kind == TopBarItemKind::SEPARATOR);
        d.linkedFlagIdx = (item.kind == TopBarItemKind::SEPARATOR) ? prevFlagIdx : -1;
        d.nextFlagIdx = -1;
        if (item.kind == TopBarItemKind::MODE_FLAG) {
            lastModeFlagIdxByPos[posIdx] = static_cast<int8_t>(myIdx);

            const int8_t pendingSepIdx = pendingSepNeedingNextByPos[posIdx];
            if (pendingSepIdx >= 0 && pendingSepIdx < static_cast<int8_t>(s_dynCount)) {
                s_dynItems[pendingSepIdx].nextFlagIdx = static_cast<int8_t>(myIdx);
            }
            pendingSepNeedingNextByPos[posIdx] = -1;
        } else if (item.kind == TopBarItemKind::SEPARATOR) {

            pendingSepNeedingNextByPos[posIdx] = static_cast<int8_t>(myIdx);
        }
    }

    if (item.kind != TopBarItemKind::MODE_FLAG && item.kind != TopBarItemKind::SEPARATOR) {
        lastModeFlagIdxByPos[posIdx] = -1;
        pendingSepNeedingNextByPos[posIdx] = -1;
    }
}

void buildFromLayout(const CfgTopBar &cfg, bool hasDayTheme) {
    s_dynCount = 0;
    lv_obj_t *prevByPos[3] = {nullptr, nullptr, nullptr};

    int8_t lastModeFlagIdxByPos[3] = {-1, -1, -1};
    int8_t pendingSepNeedingNextByPos[3] = {-1, -1, -1};

    for (uint8_t i = 0; i < cfg.itemCount; ++i) {
        const auto pos = cfg.items[i].position;
        if (pos == TopBarItemPos::LEFT || pos == TopBarItemPos::CENTER) {
            buildItem(cfg.items[i], prevByPos, lastModeFlagIdxByPos, pendingSepNeedingNextByPos,
                      hasDayTheme);
        }
    }

    for (int i = cfg.itemCount - 1; i >= 0; --i) {
        if (cfg.items[i].position == TopBarItemPos::RIGHT) {
            buildItem(cfg.items[i], prevByPos, lastModeFlagIdxByPos, pendingSepNeedingNextByPos,
                      hasDayTheme);
        }
    }
}

} // namespace

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
    lv_obj_set_style_pad_hor(s_bar, derivedPadding(s_height), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        s_bar,
        [](lv_event_t *e) {
            if (lv_event_get_target(e) != lv_event_get_current_target(e))
                return;
            if (!SettingsPage::isOpen())
                return;

            if (millis() - SettingsPage::lastOpenMs() < SETTINGS_OPEN_TAP_GUARD_MS)
                return;
            LOG_INFO("UI", "Top bar tapped — closing Settings");
            SettingsPage::close();
        },
        LV_EVENT_CLICKED, nullptr);

    s_themeIcon = nullptr;
    s_dynCount = 0;
    memset(s_dynEverSeen, 0, sizeof(s_dynEverSeen));

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
        lv_img_set_src(s_themeIcon, IconAssets::resolveSource(
                                        ThemeManager::isDayMode() ? "icon_day" : "icon_night"));
    }
}

static uint32_t statusDotColor(bool valid, bool everSeen) {
    if (valid)
        return COLOR_DOT_OK;
    return everSeen ? COLOR_DOT_STALE : COLOR_DOT_DOWN;
}

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
        return;
    lv_obj_set_style_bg_color(d.obj, lv_color_hex(color), LV_PART_MAIN);
    d.lastColor = color;
}

static void updateDynSignalLabel(DynItem &d) {
    SignalId sid = signalIdFromName(d.signalId);
    const bool valid = sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid);

    if (!valid) {
        if (!lv_obj_has_flag(d.obj, LV_OBJ_FLAG_HIDDEN))
            lv_obj_add_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lv_obj_has_flag(d.obj, LV_OBJ_FLAG_HIDDEN))
        lv_obj_clear_flag(d.obj, LV_OBJ_FLAG_HIDDEN);

    const char *fmt = d.format[0] ? d.format : "%.1f";
    char buf[16];
    const float v = SignalStore::read(sid, 0.0f);
    FloatFormat::formatFromSpec(buf, sizeof(buf), v, fmt);
    if (strcmp(buf, d.lastText) != 0) {
        lv_label_set_text(d.obj, buf);
        strlcpy(d.lastText, buf, sizeof(d.lastText));
    }
    if (COLOR_LABEL != d.lastColor) {
        lv_obj_set_style_text_color(d.obj, lv_color_hex(COLOR_LABEL), 0);
        d.lastColor = COLOR_LABEL;
    }
}

static void updateBleIcon(lv_obj_t *obj, DynItem *d) {
#if APP_BLE_ENABLED
    const bool connected = BleServer::isConnected();
    const bool advertising = !connected && BleServer::isEnabled();
#else
    const bool connected = false;
    const bool advertising = false;
#endif

    const char *text = connected ? "BLE+" : advertising ? "BLE." : "BLE";
    const uint32_t color = connected ? COLOR_BLE_CONN : advertising ? COLOR_BLE_ADV : COLOR_BLE_OFF;

    if (d != nullptr && color == d->lastColor && strcmp(d->lastText, text) == 0)
        return;

    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (d != nullptr) {
        d->lastColor = color;
        strlcpy(d->lastText, text, sizeof(d->lastText));
    }
}

static void updateModeFlag(DynItem &d) {
    SignalId sid = signalIdFromName(d.signalId);
    bool active = false;
    if (sid < SignalIds::SIGNAL_COUNT && SignalStore::isValid(sid)) {
        active = SignalStore::read(sid, 0.0f) != 0.0f;
    }
    const bool wantHidden = !active;
    if (wantHidden != d.hidden) {
        if (wantHidden) {
            lv_obj_add_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
        }
        d.hidden = wantHidden;
    }
    const uint32_t color = active ? COLOR_MODE_ACTIVE : COLOR_MODE_IDLE;
    if (color == d.lastColor)
        return;
    lv_obj_set_style_text_color(d.obj, lv_color_hex(color), 0);
    d.lastColor = color;
}

static constexpr uint32_t TRACK_BADGE_TIMEOUT_MS = 5000;

static void updateTrackBadge(DynItem &d) {
    const bool active = TrackStore::isActiveWithin(TRACK_BADGE_TIMEOUT_MS);
    const bool wantHidden = !active;
    if (wantHidden == d.hidden)
        return;
    if (wantHidden) {
        lv_obj_add_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
    }
    d.hidden = wantHidden;
}

static void updateLinkedSeparator(DynItem &d) {
    if (d.linkedFlagIdx < 0 || d.linkedFlagIdx >= static_cast<int8_t>(s_dynCount))
        return;
    const bool prevHidden = s_dynItems[d.linkedFlagIdx].hidden;
    const bool nextHidden = (d.nextFlagIdx < 0) ||
                            (d.nextFlagIdx >= static_cast<int8_t>(s_dynCount)) ||
                            s_dynItems[d.nextFlagIdx].hidden;
    const bool wantHidden = prevHidden || nextHidden;
    if (wantHidden == d.hidden)
        return;
    if (wantHidden) {
        lv_obj_add_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
    }
    d.hidden = wantHidden;
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
            case TopBarItemKind::BLE_ICON:
                updateBleIcon(d.obj, &d);
                break;
            case TopBarItemKind::MODE_FLAG:
                updateModeFlag(d);
                break;
            case TopBarItemKind::TRACK_BADGE:
                updateTrackBadge(d);
                break;
            case TopBarItemKind::SEPARATOR:
                updateLinkedSeparator(d);
                break;
            default:
                break;
        }
    }
}

int16_t TopBar::getHeight() {
    return s_height;
}
