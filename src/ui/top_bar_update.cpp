#include "top_bar.h"
#include "top_bar_internal.h"

#include "runtime/bus_health.h"
#include "runtime/signal_stats.h"
#include "top_bar_rev_limit.h"
#include "top_bar_separator_link.h"

#include "app_config.h"
#include "theme_manager.h"
#include "diag/lvgl_assert_lock.h"
#include "runtime/signal_store.h"
#include "runtime/track_store.h"
#include "can/can_manager.h"
#include "can/signal_map.h"
#if APP_BLE_ENABLED
    #include "hal/ble/ble_server.h"
#endif
#include "util/format_float.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

using namespace TopBarInternal;

static constexpr uint32_t COLOR_DOT_OK = 0x33CC44;
static constexpr uint32_t COLOR_DOT_STALE = ThemeTokens::kWarn;
static constexpr uint32_t COLOR_MODE_IDLE = 0x1C1C1C;

static constexpr uint32_t COLOR_BLE_CONN_NIGHT = 0x4499FF;
static constexpr uint32_t COLOR_BLE_CONN_DAY = 0x0044BB;
static constexpr uint32_t COLOR_BLE_ADV_NIGHT = 0x66AACC;
static constexpr uint32_t COLOR_BLE_ADV_DAY = 0x336699;

static constexpr const char *STALE_PLACEHOLDER = "- -";

static constexpr uint32_t CAN_BUS_LIVE_THRESHOLD_MS = 2000;

static bool anySignalValid() {
    static constexpr SignalId kAnyIds[] = {SignalIds::RPM, SignalIds::COOLANT_TEMP_C,
                                           SignalIds::BATTERY_VOLTS};
    if (CanManager::msSinceLastRx() > CAN_BUS_LIVE_THRESHOLD_MS)
        return false;
    return SignalStore::anyValid(kAnyIds, sizeof(kAnyIds) / sizeof(kAnyIds[0]));
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

static void applyDynTextColor(DynItem &d, uint32_t color) {
    if (color == d.lastColor)
        return;
    lv_obj_set_style_text_color(d.obj, lv_color_hex(color), 0);
    d.lastColor = color;
}

static void updateDynSignalLabel(DynItem &d) {
    SignalId sid = signalIdFromName(d.signalId);

    if (sid >= SignalIds::SIGNAL_COUNT) {
        WidgetHelpers::setVisibleIfChanged(d.obj, false);
        return;
    }
    WidgetHelpers::setVisibleIfChanged(d.obj, true);

    if (!SignalStore::isValid(sid)) {
        if (d.lastSeenValid) {
            applyDynTextColor(d, ThemeManager::getStaleTextColor());
            return;
        }
        if (strcmp(STALE_PLACEHOLDER, d.lastText) != 0) {
            lv_label_set_text(d.obj, STALE_PLACEHOLDER);
            strlcpy(d.lastText, STALE_PLACEHOLDER, sizeof(d.lastText));
        }
        applyDynTextColor(d, ThemeManager::getStaleTextColor());
        return;
    }
    d.lastSeenValid = true;

    const char *fmt = d.format[0] ? d.format : "%.1f";
    char buf[DYN_TEXT_CAP];
    const float v = SignalStore::read(sid, 0.0f);
    FloatFormat::formatFromSpec(buf, sizeof(buf), v, fmt);
    if (strcmp(buf, d.lastText) != 0) {
        lv_label_set_text(d.obj, buf);
        strlcpy(d.lastText, buf, sizeof(d.lastText));
    }
    applyDynTextColor(d, labelColor());
}

static void updateDynSignalMax(DynItem &d) {
    const SignalId sid = signalIdFromName(d.signalId);
    if (sid >= SignalIds::SIGNAL_COUNT) {
        WidgetHelpers::setVisibleIfChanged(d.obj, false);
        return;
    }
    WidgetHelpers::setVisibleIfChanged(d.obj, true);
    const bool known = SignalStats::hasMax(sid);
    char buf[DYN_TEXT_CAP];
    if (!known) {
        snprintf(buf, sizeof(buf), "%s %s", d.prefix, STALE_PLACEHOLDER);
    } else {
        const char *fmt = d.format[0] ? d.format : "%.0f";
        char value[DYN_TEXT_CAP];
        FloatFormat::formatFromSpec(value, sizeof(value), SignalStats::maxValue(sid), fmt);
        snprintf(buf, sizeof(buf), "%s %s", d.prefix, value);
    }
    if (strcmp(buf, d.lastText) != 0) {
        lv_label_set_text(d.obj, buf);
        strlcpy(d.lastText, buf, sizeof(d.lastText));
    }
    applyDynTextColor(d, known ? labelColor() : ThemeManager::getStaleTextColor());
}

static void updateBleIcon(lv_obj_t *obj, DynItem *d) {
#if APP_BLE_ENABLED
    const bool connected = BleServer::isConnected();
    const bool advertising = !connected && BleServer::isEnabled();
#else
    const bool connected = false;
    const bool advertising = false;
#endif

    const char *text = connected ? "• BLE" : "BLE";
    const uint32_t color =
        connected     ? ThemeManager::pickColor(COLOR_BLE_CONN_NIGHT, COLOR_BLE_CONN_DAY)
        : advertising ? ThemeManager::pickColor(COLOR_BLE_ADV_NIGHT, COLOR_BLE_ADV_DAY)
                      : ThemeManager::getStaleTextColor();

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
        WidgetHelpers::setVisible(d.obj, active);
        d.hidden = wantHidden;
    }
    applyDynTextColor(d, active ? COLOR_MODE_ACTIVE : COLOR_MODE_IDLE);
}

static constexpr uint32_t TRACK_BADGE_TIMEOUT_MS = 5000;

static uint32_t busFieldColor(bool silent) {
    return silent ? ThemeManager::warnColor() : labelColor();
}

static uint32_t staticLabelColor(const DynItem &d, bool silent) {
    if (silent && d.position == TopBarItemPos::LEFT)
        return ThemeManager::warnColor();
    return mutedColor();
}

static void updateCanRate(DynItem &d, bool silent) {
    char buf[DYN_TEXT_CAP];
    if (silent) {
        strlcpy(buf, BUS_RATE_SILENT, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%lu Hz", static_cast<unsigned long>(CanManager::busRateHz()));
    }
    if (strcmp(buf, d.lastText) != 0) {
        lv_label_set_text(d.obj, buf);
        strlcpy(d.lastText, buf, sizeof(d.lastText));
    }
    applyDynTextColor(d, busFieldColor(silent));
}

static void updateTrackBadge(DynItem &d) {
    const bool active = TrackStore::isActiveWithin(TRACK_BADGE_TIMEOUT_MS);
    const bool wantHidden = !active;
    if (wantHidden != d.hidden) {
        WidgetHelpers::setVisible(d.obj, active);
        d.hidden = wantHidden;
    }
    if (wantHidden)
        return;

    TrackStore::State st;
    TrackStore::snapshot(&st);
    const uint32_t ms = st.currentLapMs;
    char buf[24];
    snprintf(buf, sizeof(buf), "LAP %u   %u:%02u.%02u", static_cast<unsigned>(st.lapNumber),
             static_cast<unsigned>(ms / 60000u), static_cast<unsigned>((ms % 60000u) / 1000u),
             static_cast<unsigned>((ms % 1000u) / 10u));
    if (strcmp(buf, d.lastText) != 0) {
        lv_label_set_text(d.obj, buf);
        strlcpy(d.lastText, buf, sizeof(d.lastText));
    }
    applyDynTextColor(d, labelColor());
}

static void updateLinkedSeparator(DynItem &d) {
    const int8_t dynCount = static_cast<int8_t>(s_dynCount);
    if (!TopBarSeparatorLink::hasValidLink(d.linkedFlagIdx, dynCount))
        return;
    applyDynTextColor(d, mutedColor());
    const bool linkedHidden = s_dynItems[d.linkedFlagIdx].hidden;
    const bool nextValid = d.nextFlagIdx >= 0 && d.nextFlagIdx < dynCount;
    const bool nextFlagHidden = nextValid && s_dynItems[d.nextFlagIdx].hidden;
    const bool wantHidden =
        TopBarSeparatorLink::wantsHidden(d.nextFlagIdx, dynCount, linkedHidden, nextFlagHidden);
    if (wantHidden == d.hidden)
        return;
    WidgetHelpers::setVisible(d.obj, !wantHidden);
    d.hidden = wantHidden;
}

void TopBar::update() {
    LVGL_ASSERT_LOCKED();
    if (!s_bar)
        return;

    const bool silent = BusHealth::sample().silent;
    const bool revLimitOverride = TopBarRevLimit::apply();

    for (uint8_t i = 0; i < s_dynCount; ++i) {
        DynItem &d = s_dynItems[i];
        if (revLimitOverride && d.position == TopBarItemPos::RIGHT)
            continue;
        switch (d.kind) {
            case TopBarItemKind::STATUS_DOT:
                updateDynStatusDot(i, d);
                break;
            case TopBarItemKind::SIGNAL:
                updateDynSignalLabel(d);
                break;
            case TopBarItemKind::SIGNAL_MAX:
                updateDynSignalMax(d);
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
            case TopBarItemKind::CAN_RATE:
                updateCanRate(d, silent);
                break;
            case TopBarItemKind::SEPARATOR:
                updateLinkedSeparator(d);
                break;
            case TopBarItemKind::LABEL:
                applyDynTextColor(d, staticLabelColor(d, silent));
                break;
            default:
                break;
        }
    }
}
