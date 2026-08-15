#include "top_bar.h"
#include "top_bar_internal.h"
#include "top_bar_rev_limit.h"
#include "top_bar_separator_link.h"

#include "app_config.h"
#include "icon_assets.h"
#include "layout_scale.h"
#include "ui/font_manager.h"
#include "settings_page.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "diag/logger.h"
#include "ui/widgets/widget_helpers.h"

#include <lvgl.h>
#include <math.h>
#include <string.h>

using namespace TopBarInternal;

namespace TopBarInternal {
lv_obj_t *s_bar = nullptr;
DynItem s_dynItems[CFG_MAX_TOPBAR_ITEMS];
uint8_t s_dynCount = 0;
bool s_dynEverSeen[CFG_MAX_TOPBAR_ITEMS] = {};
} // namespace TopBarInternal

namespace TopBarMetrics {
constexpr float DOT_RATIO = 0.30f;
constexpr float GAP_RATIO = 0.25f;
constexpr float PADDING_RATIO = 0.40f;
} // namespace TopBarMetrics

static inline int16_t metricRound(float v) {
    return static_cast<int16_t>(lroundf(v));
}
static inline int16_t derivedDot(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::DOT_RATIO);
}
static inline int16_t derivedGap(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::GAP_RATIO);
}
static inline int16_t derivedPadding(int16_t height) {
    return metricRound(static_cast<float>(height) * TopBarMetrics::PADDING_RATIO);
}
static lv_obj_t *s_themeIcon = nullptr;
static int16_t s_height = 30;
static bool s_pageOverridden = false;

static constexpr uint32_t COLOR_BAR_BG_DAY = 0xF0F0F0;

static uint32_t barBgColor(const CfgTopBar &cfg) {
    return ThemeManager::pickColor(cfg.bgColor.rgb, COLOR_BAR_BG_DAY);
}

static lv_obj_t *makeStatusDot(lv_obj_t *parent) {
    const int16_t side = derivedDot(s_height);
    return WidgetHelpers::makeSquareBadge(parent, side, COLOR_DOT_DOWN);
}

static lv_obj_t *makeFlagBadge(lv_obj_t *parent, const char *text) {
    lv_obj_t *cont = lv_obj_create(parent);
    WidgetHelpers::resetContainerStyle(cont);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cont, LayoutScale::x(FLAG_GAP_PX), LV_PART_MAIN);
    WidgetHelpers::makeSquareBadge(cont, FLAG_SQUARE_PX, COLOR_MODE_ACTIVE);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FontManager::units(), 0);
    lv_obj_set_style_text_color(cont, lv_color_hex(COLOR_MODE_ACTIVE), 0);
    lv_obj_update_layout(cont);
    return cont;
}

static lv_obj_t *makeBarLabel(lv_obj_t *parent, const char *text, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);

    lv_obj_set_style_text_font(lbl, FontManager::units(), 0);
    lv_obj_set_style_text_letter_space(lbl, BAR_LETTER_SPACE_PX, 0);
    return lbl;
}

static lv_obj_t *makeBarSeparator(lv_obj_t *parent, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "|");
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, FontManager::units(), 0);
    return lbl;
}

namespace {

void buildItem(const CfgTopBarItem &item, lv_obj_t *prevByPos[3],
               TopBarSeparatorLink::Tracker trackers[3], bool hasDayTheme) {
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
            obj = makeBarLabel(s_bar, item.text, mutedColor());
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::SEPARATOR: {
            obj = makeBarSeparator(s_bar, mutedColor());

            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::SIGNAL:
        case TopBarItemKind::SIGNAL_MAX: {

            obj = makeBarLabel(s_bar, "", labelColor());
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::BLE_ICON: {
            obj = lv_label_create(s_bar);
            lv_label_set_text(obj, "BLE");
            lv_obj_set_style_text_color(obj, lv_color_hex(ThemeManager::getStaleTextColor()), 0);
            lv_obj_set_style_text_font(obj, FontManager::units(), 0);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::MODE_FLAG: {
            obj = makeFlagBadge(s_bar, item.text);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::TRACK_BADGE: {
            obj = makeBarLabel(s_bar, "", labelColor());
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            anchor(obj, gap);
            break;
        }
        case TopBarItemKind::CAN_RATE: {
            obj = makeBarLabel(s_bar, "-- Hz", labelColor());
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
    const int8_t prevFlagIdx = TopBarSeparatorLink::linkedFlagForSeparator(trackers[posIdx]);
    bool needsUpdate =
        (item.kind == TopBarItemKind::STATUS_DOT || item.kind == TopBarItemKind::SIGNAL ||
         item.kind == TopBarItemKind::SIGNAL_MAX || item.kind == TopBarItemKind::BLE_ICON ||
         item.kind == TopBarItemKind::MODE_FLAG || item.kind == TopBarItemKind::TRACK_BADGE ||
         item.kind == TopBarItemKind::CAN_RATE || item.kind == TopBarItemKind::LABEL ||
         (item.kind == TopBarItemKind::SEPARATOR && prevFlagIdx >= 0));
    if (needsUpdate && s_dynCount < CFG_MAX_TOPBAR_ITEMS) {
        const uint8_t myIdx = s_dynCount;
        DynItem &d = s_dynItems[s_dynCount++];
        d.kind = item.kind;
        d.position = item.position;
        d.obj = obj;
        strlcpy(d.signalId, item.signalId, sizeof(d.signalId));
        strlcpy(d.format, item.format, sizeof(d.format));
        strlcpy(d.prefix, item.text, sizeof(d.prefix));
        d.lastText[0] = '\0';
        d.lastColor = COLOR_UNSET;
        d.lastSeenValid = false;

        d.hidden =
            (item.kind == TopBarItemKind::MODE_FLAG || item.kind == TopBarItemKind::SEPARATOR);
        d.linkedFlagIdx =
            (item.kind == TopBarItemKind::SEPARATOR) ? prevFlagIdx : TopBarSeparatorLink::NO_FLAG;
        d.nextFlagIdx = TopBarSeparatorLink::NO_FLAG;
        if (item.kind == TopBarItemKind::MODE_FLAG) {
            const int8_t pendingSepIdx = TopBarSeparatorLink::registerModeFlag(
                trackers[posIdx], static_cast<int8_t>(myIdx), static_cast<int8_t>(s_dynCount));
            if (pendingSepIdx != TopBarSeparatorLink::NO_FLAG) {
                s_dynItems[pendingSepIdx].nextFlagIdx = static_cast<int8_t>(myIdx);
            }
        } else if (item.kind == TopBarItemKind::SEPARATOR) {
            TopBarSeparatorLink::registerSeparator(trackers[posIdx], static_cast<int8_t>(myIdx));
        }
    }

    if (item.kind != TopBarItemKind::MODE_FLAG && item.kind != TopBarItemKind::SEPARATOR) {
        TopBarSeparatorLink::resetTracking(trackers[posIdx]);
    }
}

void buildFromLayout(const CfgTopBar &cfg, bool hasDayTheme) {
    s_dynCount = 0;
    lv_obj_t *prevByPos[3] = {nullptr, nullptr, nullptr};

    TopBarSeparatorLink::Tracker trackers[3];

    for (uint8_t i = 0; i < cfg.itemCount; ++i) {
        const auto pos = cfg.items[i].position;
        if (pos == TopBarItemPos::LEFT || pos == TopBarItemPos::CENTER) {
            buildItem(cfg.items[i], prevByPos, trackers, hasDayTheme);
        }
    }

    for (int i = cfg.itemCount - 1; i >= 0; --i) {
        if (cfg.items[i].position == TopBarItemPos::RIGHT) {
            buildItem(cfg.items[i], prevByPos, trackers, hasDayTheme);
        }
    }
}

void clearDynItems() {
    TopBarRevLimit::clearOverride();
    for (uint8_t i = 0; i < s_dynCount; ++i) {
        if (s_dynItems[i].obj)
            lv_obj_del(s_dynItems[i].obj);
    }
    s_dynCount = 0;
    s_themeIcon = nullptr;
    memset(s_dynEverSeen, 0, sizeof(s_dynEverSeen));
}

// The status row's centre and right fields are per page (§8 of the design
// system); the left field is always the bus rate.
uint8_t mergePageStatusRow(const CfgTopBar &base, const CfgStatusRow &row, CfgTopBarItem *out) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < base.itemCount && n < CFG_MAX_TOPBAR_ITEMS; ++i) {
        const CfgTopBarItem &item = base.items[i];
        if (row.hasCenter && item.position == TopBarItemPos::CENTER)
            continue;
        if (row.hasRight && item.position == TopBarItemPos::RIGHT)
            continue;
        out[n++] = item;
    }
    if (row.hasCenter && n < CFG_MAX_TOPBAR_ITEMS)
        out[n++] = row.center;
    if (row.hasRight && n < CFG_MAX_TOPBAR_ITEMS)
        out[n++] = row.right;
    return n;
}

} // namespace

void TopBar::applyPage(const CfgPage &page) {
    if (!s_bar)
        return;
    if (!page.statusRow.hasCenter && !page.statusRow.hasRight && !s_pageOverridden)
        return;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    CfgTopBar effective = dash.topBar;
    effective.itemCount = mergePageStatusRow(dash.topBar, page.statusRow, effective.items);

    clearDynItems();
    buildFromLayout(effective, dash.hasDayTheme);
    s_pageOverridden = page.statusRow.hasCenter || page.statusRow.hasRight;
    TopBar::update();
}

void TopBar::rebuild() {
    clearDynItems();
    if (s_bar) {
        lv_obj_del(s_bar);
        s_bar = nullptr;
    }
    TopBar::init();
}

void TopBar::init() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    const CfgTopBar &cfg = dash.topBar;
    s_height = cfg.height > 0 ? cfg.height : 30;

    s_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_bar, LV_HOR_RES, s_height);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(barBgColor(cfg)), LV_PART_MAIN);
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
    TopBarRevLimit::build(s_bar);

    SettingsPage::init(s_height, static_cast<int16_t>(LV_VER_RES - s_height));

    LOG_INFO("UI", "Top bar initialized (height=%dpx, dyn=%u)", s_height,
             static_cast<unsigned>(s_dynCount));
}

void TopBar::reapplyTheme() {
    if (!s_bar)
        return;
    const CfgTopBar &cfg = ConfigLoader::getDashboardConfig().topBar;
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(barBgColor(cfg)), LV_PART_MAIN);
    if (s_themeIcon) {
        lv_img_set_src(s_themeIcon, IconAssets::resolveSource(
                                        ThemeManager::isDayMode() ? "icon_day" : "icon_night"));
    }
    for (uint8_t i = 0; i < s_dynCount; ++i) {
        s_dynItems[i].lastColor = COLOR_UNSET;
    }
    TopBar::update();
}

int16_t TopBar::getHeight() {
    return s_height;
}
