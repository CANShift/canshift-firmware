#include "top_bar.h"
#include "top_bar_internal.h"
#include "top_bar_rev_limit.h"
#include "top_bar_separator_link.h"
#include "top_bar_items.h"

#include "app_config.h"
#include "icon_assets.h"
#include "settings_page.h"
#include "theme_manager.h"
#include "config/config_loader.h"
#include "diag/logger.h"

#include <lvgl.h>
#include <string.h>

using namespace TopBarInternal;

namespace TopBarInternal {
lv_obj_t *s_bar = nullptr;
DynItem s_dynItems[kCfgMaxTopbarItems];
uint8_t s_dynCount = 0;
bool s_dynEverSeen[kCfgMaxTopbarItems] = {};
lv_obj_t *s_themeIcon = nullptr;
int16_t s_height = 30;
}

static bool s_pageOverridden = false;

static constexpr uint32_t kColorBarBgDay = 0xF0F0F0;

static uint32_t barBgColor(const CfgTopBar &cfg) {
    return ThemeManager::pickColor(cfg.bgColor.rgb, kColorBarBgDay);
}

namespace {

constexpr lv_align_t kFirstItemAlign[] = {LV_ALIGN_LEFT_MID, LV_ALIGN_CENTER, LV_ALIGN_RIGHT_MID};

void anchorItem(lv_obj_t *obj, lv_obj_t *prev, TopBarItemPos position, int16_t gap) {
    if (prev == nullptr) {
        lv_obj_align(obj, kFirstItemAlign[static_cast<uint8_t>(position)], 0, 0);
        return;
    }
    if (position == TopBarItemPos::RIGHT) {
        lv_obj_align_to(obj, prev, LV_ALIGN_OUT_LEFT_MID, -gap, 0);
        return;
    }
    lv_obj_align_to(obj, prev, LV_ALIGN_OUT_RIGHT_MID, gap, 0);
}

void fillDynItem(DynItem &d, const CfgTopBarItem &item, lv_obj_t *obj, int8_t prevFlagIdx) {
    const bool isSeparator = item.kind == TopBarItemKind::SEPARATOR;
    d.kind = item.kind;
    d.position = item.position;
    d.obj = obj;
    strlcpy(d.signalId, item.signalId, sizeof(d.signalId));
    strlcpy(d.format, item.format, sizeof(d.format));
    strlcpy(d.prefix, item.text, sizeof(d.prefix));
    d.lastText[0] = '\0';
    d.lastColor = kColorUnset;
    d.lastSeenValid = false;
    d.hidden = item.kind == TopBarItemKind::MODE_FLAG || isSeparator;
    d.linkedFlagIdx = isSeparator ? prevFlagIdx : TopBarSeparatorLink::kNoFlag;
    d.nextFlagIdx = TopBarSeparatorLink::kNoFlag;
}

void linkModeFlag(TopBarSeparatorLink::Tracker &tracker, uint8_t flagIdx) {
    const int8_t pendingSepIdx = TopBarSeparatorLink::registerModeFlag(
        tracker, static_cast<int8_t>(flagIdx), static_cast<int8_t>(s_dynCount));
    if (pendingSepIdx != TopBarSeparatorLink::kNoFlag) {
        s_dynItems[pendingSepIdx].nextFlagIdx = static_cast<int8_t>(flagIdx);
    }
}

void trackDynItem(const CfgTopBarItem &item, lv_obj_t *obj, TopBarSeparatorLink::Tracker &tracker,
                  int8_t prevFlagIdx) {
    const uint8_t myIdx = s_dynCount;
    fillDynItem(s_dynItems[s_dynCount++], item, obj, prevFlagIdx);
    if (item.kind == TopBarItemKind::MODE_FLAG) {
        linkModeFlag(tracker, myIdx);
        return;
    }
    if (item.kind == TopBarItemKind::SEPARATOR) {
        TopBarSeparatorLink::registerSeparator(tracker, static_cast<int8_t>(myIdx));
    }
}

void buildItem(const CfgTopBarItem &item, lv_obj_t *prevByPos[3],
               TopBarSeparatorLink::Tracker trackers[3], bool hasTheme) {
    const TopBarItems::KindOps *ops = TopBarItems::opsFor(item.kind);
    if (!ops)
        return;
    const uint8_t posIdx = static_cast<uint8_t>(item.position);
    lv_obj_t *obj = ops->build(item, hasTheme);
    anchorItem(obj, prevByPos[posIdx], item.position,
               ops->gapped ? TopBarItems::derivedGap(s_height) : 0);
    prevByPos[posIdx] = obj;

    TopBarSeparatorLink::Tracker &tracker = trackers[posIdx];
    const int8_t prevFlagIdx = TopBarSeparatorLink::linkedFlagForSeparator(tracker);
    const bool linkedSeparator = item.kind == TopBarItemKind::SEPARATOR && prevFlagIdx >= 0;
    if ((ops->tracked || linkedSeparator) && s_dynCount < kCfgMaxTopbarItems) {
        trackDynItem(item, obj, tracker, prevFlagIdx);
    }
    if (item.kind != TopBarItemKind::MODE_FLAG && item.kind != TopBarItemKind::SEPARATOR) {
        TopBarSeparatorLink::resetTracking(tracker);
    }
}

void buildFromLayout(const CfgTopBar &cfg, bool hasTheme) {
    s_dynCount = 0;
    lv_obj_t *prevByPos[3] = {nullptr, nullptr, nullptr};

    TopBarSeparatorLink::Tracker trackers[3];

    for (uint8_t i = 0; i < cfg.itemCount; ++i) {
        const auto pos = cfg.items[i].position;
        if (pos == TopBarItemPos::LEFT || pos == TopBarItemPos::CENTER) {
            buildItem(cfg.items[i], prevByPos, trackers, hasTheme);
        }
    }

    for (int i = cfg.itemCount - 1; i >= 0; --i) {
        if (cfg.items[i].position == TopBarItemPos::RIGHT) {
            buildItem(cfg.items[i], prevByPos, trackers, hasTheme);
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

uint8_t mergePageStatusRow(const CfgTopBar &base, const CfgStatusRow &row, CfgTopBarItem *out) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < base.itemCount && n < kCfgMaxTopbarItems; ++i) {
        const CfgTopBarItem &item = base.items[i];
        if (row.hasCenter && item.position == TopBarItemPos::CENTER)
            continue;
        if (row.hasRight && item.position == TopBarItemPos::RIGHT)
            continue;
        out[n++] = item;
    }
    if (row.hasCenter && n < kCfgMaxTopbarItems)
        out[n++] = row.center;
    if (row.hasRight && n < kCfgMaxTopbarItems)
        out[n++] = row.right;
    return n;
}

}

void TopBar::applyPage(const CfgPage &page) {
    if (!s_bar)
        return;
    if (!page.statusRow.hasCenter && !page.statusRow.hasRight && !s_pageOverridden)
        return;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    CfgTopBar effective = dash.topBar;
    effective.itemCount = mergePageStatusRow(dash.topBar, page.statusRow, effective.items);

    clearDynItems();
    buildFromLayout(effective, dash.hasTheme);
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
    lv_obj_set_style_pad_hor(s_bar, TopBarItems::derivedPadding(s_height), LV_PART_MAIN);
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

            if (millis() - SettingsPage::lastOpenMs() < kSettingsOpenTapGuardMs)
                return;
            LOG_INFO("UI", "Top bar tapped — closing Settings");
            SettingsPage::close();
        },
        LV_EVENT_CLICKED, nullptr);

    s_themeIcon = nullptr;
    s_dynCount = 0;
    memset(s_dynEverSeen, 0, sizeof(s_dynEverSeen));

    buildFromLayout(cfg, dash.hasTheme);
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
        s_dynItems[i].lastColor = kColorUnset;
    }
    TopBar::update();
}

int16_t TopBar::getHeight() {
    return s_height;
}
