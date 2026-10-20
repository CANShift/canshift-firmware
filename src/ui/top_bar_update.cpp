#include "top_bar.h"
#include "top_bar_internal.h"

#include "runtime/bus_health.h"
#include "runtime/signal_stats.h"
#include "top_bar_rev_limit.h"
#include "top_bar_separator_link.h"
#include "top_bar_status_update.h"

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
#include "ui/status_field_text.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

using namespace TopBarInternal;

static constexpr uint32_t kColorDotOk = 0x33CC44;
static constexpr uint32_t kColorDotStale = ThemeTokens::kWarn;
static constexpr uint32_t kColorModeIdle = 0x1C1C1C;

static constexpr uint32_t kColorBleConnNight = 0x4499FF;
static constexpr uint32_t kColorBleConnDay = 0x0044BB;
static constexpr uint32_t kColorBleAdvNight = 0x66AACC;
static constexpr uint32_t kColorBleAdvDay = 0x336699;

static constexpr uint32_t kCanBusLiveThresholdMs = 2000;

static bool anySignalValid() {
    static constexpr SignalId kAnyIds[] = {SignalIds::kRpm, SignalIds::kCoolantTempC,
                                           SignalIds::kBatteryVolts};
    if (CanManager::msSinceLastRx() > kCanBusLiveThresholdMs)
        return false;
    return SignalStore::anyValid(kAnyIds, sizeof(kAnyIds) / sizeof(kAnyIds[0]));
}

static uint32_t statusDotColor(bool valid, bool everSeen) {
    if (valid)
        return kColorDotOk;
    return everSeen ? kColorDotStale : kColorDotDown;
}

static void updateDynStatusDot(uint8_t idx, DynItem &d, bool) {
    bool valid = false;
    if (d.item.signalId[0] == '\0' || strcmp(d.item.signalId, "any") == 0) {
        valid = anySignalValid();
    } else {
        SignalId sid = signalIdFromName(d.item.signalId);
        valid = (sid < SignalIds::kSignalCount) && SignalStore::isValid(sid);
    }
    if (valid)
        s_dynEverSeen[idx] = true;
    const uint32_t color = statusDotColor(valid, s_dynEverSeen[idx]);
    if (color == d.lastColor)
        return;
    lv_obj_set_style_bg_color(d.obj, lv_color_hex(color), LV_PART_MAIN);
    d.lastColor = color;
}

static void updateDynSignalLabel(uint8_t, DynItem &d, bool) {
    SignalId sid = signalIdFromName(d.item.signalId);

    if (sid >= SignalIds::kSignalCount) {
        WidgetHelpers::setVisibleIfChanged(d.obj, false);
        return;
    }
    WidgetHelpers::setVisibleIfChanged(d.obj, true);

    if (!SignalStore::isValid(sid)) {
        if (!d.lastSeenValid)
            applyDynText(d, StatusFieldText::kStalePlaceholder);
        applyDynTextColor(d, ThemeManager::getStaleTextColor());
        return;
    }
    d.lastSeenValid = true;

    const char *fmt = d.item.format[0] ? d.item.format : "%.1f";
    char buf[kDynTextCap];
    StatusFieldText::formatValue(buf, sizeof(buf), SignalStore::readDisplay(sid, 0.0f), fmt,
                                 d.item);
    applyDynText(d, buf);
    applyDynTextColor(d, labelColor());
}

static void formatSignalMax(char *buf, size_t cap, SignalId sid, const DynItem &d) {
    if (!SignalStats::hasMax(sid)) {
        StatusFieldText::withPrefix(buf, cap, d.item.text, StatusFieldText::kStalePlaceholder);
        return;
    }
    const char *fmt = d.item.format[0] ? d.item.format : "%.0f";
    char value[kDynTextCap];
    StatusFieldText::formatValue(value, sizeof(value), SignalStats::maxValue(sid), fmt, d.item);
    StatusFieldText::withPrefix(buf, cap, d.item.text, value);
}

static void updateDynSignalMax(uint8_t, DynItem &d, bool) {
    const SignalId sid = signalIdFromName(d.item.signalId);
    if (sid >= SignalIds::kSignalCount) {
        WidgetHelpers::setVisibleIfChanged(d.obj, false);
        return;
    }
    WidgetHelpers::setVisibleIfChanged(d.obj, true);
    char buf[kDynTextCap];
    formatSignalMax(buf, sizeof(buf), sid, d);
    applyDynText(d, buf);
    applyDynTextColor(d,
                      SignalStats::hasMax(sid) ? labelColor() : ThemeManager::getStaleTextColor());
}

static void updateBleIcon(uint8_t, DynItem &d, bool) {
#if APP_BLE_ENABLED
    const bool connected = BleServer::isConnected();
    const bool advertising = !connected && BleServer::isEnabled();
#else
    const bool connected = false;
    const bool advertising = false;
#endif

    const char *text = connected ? "• BLE" : "BLE";
    const uint32_t color = connected ? ThemeManager::pickColor(kColorBleConnNight, kColorBleConnDay)
                           : advertising
                               ? ThemeManager::pickColor(kColorBleAdvNight, kColorBleAdvDay)
                               : ThemeManager::getStaleTextColor();

    if (color == d.lastColor && strcmp(d.lastText, text) == 0)
        return;

    lv_label_set_text(d.obj, text);
    lv_obj_set_style_text_color(d.obj, lv_color_hex(color), 0);
    d.lastColor = color;
    strlcpy(d.lastText, text, sizeof(d.lastText));
}

static void updateModeFlag(uint8_t, DynItem &d, bool) {
    SignalId sid = signalIdFromName(d.item.signalId);
    bool active = false;
    if (sid < SignalIds::kSignalCount && SignalStore::isValid(sid)) {
        active = SignalStore::read(sid, 0.0f) != 0.0f;
    }
    const bool wantHidden = !active;
    if (wantHidden != d.hidden) {
        WidgetHelpers::setVisible(d.obj, active);
        d.hidden = wantHidden;
    }
    applyDynTextColor(d, active ? kColorModeActive : kColorModeIdle);
}

static uint32_t busFieldColor(bool silent) {
    return silent ? ThemeManager::warnColor() : labelColor();
}

static uint32_t staticLabelColor(const DynItem &d, bool silent) {
    if (silent && d.position == TopBarItemPos::LEFT)
        return ThemeManager::warnColor();
    return mutedColor();
}

static void updateCanRate(uint8_t, DynItem &d, bool silent) {
    char buf[kDynTextCap];
    if (silent) {
        strlcpy(buf, kBusRateSilent, sizeof(buf));
    } else {
        snprintf(buf, sizeof(buf), "%lu Hz", static_cast<unsigned long>(CanManager::busRateHz()));
    }
    applyDynText(d, buf);
    applyDynTextColor(d, busFieldColor(silent));
}

static void updateTrackBadge(uint8_t, DynItem &d, bool) {
    const bool active = TrackStore::isActiveWithin(kTrackTelemetryTimeoutMs);
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
    applyDynText(d, buf);
    applyDynTextColor(d, labelColor());
}

static void updateLinkedSeparator(uint8_t, DynItem &d, bool) {
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

using DynUpdater = void (*)(uint8_t idx, DynItem &d, bool silent);

static void updateStaticLabel(uint8_t, DynItem &d, bool silent) {
    applyDynTextColor(d, staticLabelColor(d, silent));
}

struct DynUpdaterRow {
    TopBarItemKind kind;
    DynUpdater update;
};

static constexpr DynUpdaterRow kDynUpdaters[] = {
    {TopBarItemKind::UNKNOWN, nullptr},
    {TopBarItemKind::STATUS_DOT, updateDynStatusDot},
    {TopBarItemKind::LABEL, updateStaticLabel},
    {TopBarItemKind::SEPARATOR, updateLinkedSeparator},
    {TopBarItemKind::SIGNAL, updateDynSignalLabel},
    {TopBarItemKind::BLE_ICON, updateBleIcon},
    {TopBarItemKind::THEME_TOGGLE, nullptr},
    {TopBarItemKind::MODE_FLAG, updateModeFlag},
    {TopBarItemKind::TRACK_BADGE, updateTrackBadge},
    {TopBarItemKind::CAN_RATE, updateCanRate},
    {TopBarItemKind::SIGNAL_MAX, updateDynSignalMax},
    {TopBarItemKind::TIMER, TopBarStatusUpdate::updateTimer},
    {TopBarItemKind::FLAG_WORD, TopBarStatusUpdate::updateFlagWord},
};

static constexpr size_t kDynUpdaterCount = sizeof(kDynUpdaters) / sizeof(kDynUpdaters[0]);

static_assert(kDynUpdaterCount == static_cast<size_t>(TopBarItemKind::FLAG_WORD) + 1,
              "every top bar item kind needs a row");

static constexpr bool rowsFollowKindOrder(size_t i = 0) {
    return i == kDynUpdaterCount ||
           (static_cast<size_t>(kDynUpdaters[i].kind) == i && rowsFollowKindOrder(i + 1));
}

static_assert(rowsFollowKindOrder(), "top bar updater rows must follow TopBarItemKind order");

static DynUpdater updaterFor(TopBarItemKind kind) {
    const size_t idx = static_cast<size_t>(kind);
    return idx < kDynUpdaterCount ? kDynUpdaters[idx].update : nullptr;
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
        const DynUpdater update = updaterFor(d.kind);
        if (update)
            update(i, d, silent);
    }
}
