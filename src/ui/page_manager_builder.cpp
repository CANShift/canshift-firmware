
#include "page_manager_internal.h"

#include "bus_silent_line.h"
#include "burn_overlay.h"
#include "diag_drawer.h"
#include "error_bar.h"
#include "icon_assets.h"
#include "dash_metrics.h"
#include "theme_manager.h"
#include "theme_tokens.h"
#include "top_bar.h"
#include "diag/lvgl_assert_lock.h"
#include "page_manager.h"
#include "setup_screen.h"
#include "widget_factory.h"
#include "widgets/cruise_control_widget.h"

#include "config/config_loader.h"
#include "diag/logger.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <string.h>

namespace PageManagerInternal {

bool pageDeclaresShiftStrip(const CfgPage &cfg) {
    for (uint8_t i = 0; i < cfg.widgetCount; ++i) {
        if (cfg.widgets[i].type == WidgetType::SHIFT_LIGHT)
            return true;
    }
    return false;
}

namespace {

void applyPageBackground(lv_obj_t *screen, const CfgPage &cfg, const CfgColor &effectiveBg) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(effectiveBg.rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    if (strlen(cfg.bgImagePath) > 0) {

        static char lvglPath[CFG_MAX_PATH_LEN + 4];
        snprintf(lvglPath, sizeof(lvglPath), "S:%s", cfg.bgImagePath);

        lv_obj_t *img = lv_img_create(screen);
        lv_img_set_src(img, lvglPath);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_opa(img, LV_OPA_COVER, LV_PART_MAIN);
        LOG_DEBUG("UI", "Background image: %s", lvglPath);
    }
}

} // namespace

void buildPage(uint8_t idx, const CfgPage &cfg) {
    LOG_INFO("UI", "buildPage(%s) entry: heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    Page &p = s_pages[idx];
    strlcpy(p.id, cfg.id, CFG_MAX_ID_LEN);

    p.screen = lv_obj_create(nullptr);
    lv_obj_set_size(p.screen, LV_HOR_RES, LV_VER_RES);
    lv_obj_clear_flag(p.screen, LV_OBJ_FLAG_SCROLLABLE);

    applyPageBackground(p.screen, cfg, ThemeManager::getEffectiveBgColor(cfg.bgColor));

    const bool hasStrip = pageDeclaresShiftStrip(cfg);
    const int16_t stripBand = hasStrip ? DashMetrics::kShiftStripBandPx : 0;
    const int16_t topBarBand = cfg.showTopBar ? TopBar::getHeight() : 0;
    const int16_t contentY = static_cast<int16_t>(topBarBand + stripBand);

    if (cfg.templateKind == CfgPageTemplate::CRUISE_CONTROL) {
        CruiseControlWidget::build(p.screen, cfg, contentY);
        p.built = true;
        if (cfg.widgetCount > 0) {
            LOG_INFO("UI", "Page '%s': cruise_control template — ignoring %u user widget(s)",
                     cfg.id, static_cast<unsigned>(cfg.widgetCount));
        }
        LOG_INFO("UI", "buildPage(%s) exit:  heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(ESP.getFreeHeap()),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        return;
    }

    uint8_t created = 0;
    for (uint8_t w = 0; w < cfg.widgetCount; ++w) {
        const CfgWidget &wCfg = cfg.widgets[w];
        const int16_t yOffset = wCfg.type == WidgetType::SHIFT_LIGHT ? topBarBand : contentY;
        if (WidgetFactory::create(p.screen, wCfg, yOffset) != nullptr)
            ++created;
    }

    p.built = true;
    if (created < cfg.widgetCount) {
        LOG_WARN("UI", "Page '%s': only %u/%u widgets built — see prior WF errors", cfg.id,
                 static_cast<unsigned>(created), static_cast<unsigned>(cfg.widgetCount));
    } else {
        LOG_INFO("UI", "Built page '%s' with %u widgets", cfg.id,
                 static_cast<unsigned>(cfg.widgetCount));
    }

    LOG_INFO("UI", "buildPage(%s) exit:  heap.largest=%u heap.free=%u stack.hwm=%u", cfg.id,
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(ESP.getFreeHeap()),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void releasePage(Page &p) {
    if (!p.screen)
        return;
    WidgetFactory::clearAll(p.screen);
    lv_obj_del(p.screen);
    p.screen = nullptr;
    p.built = false;
}

uint8_t defaultPageIndex() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (strcmp(s_pages[i].id, dash.defaultPageId) == 0) {
            return i;
        }
    }
    return 0;
}

void buildPageList() {
    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    const char *defaultId = dash.defaultPageId;

    s_pageCount = 0;
    s_currentIdx = 0;

    for (uint8_t i = 0; i < dash.pageCount && s_pageCount < MAX_PAGES; ++i) {
        if (!dash.pages[i].visible) {
            LOG_INFO("UI", "Skipping hidden page '%s' (visible=false)", dash.pages[i].id);
            continue;
        }
        Page &p = s_pages[s_pageCount];
        strlcpy(p.id, dash.pages[i].id, CFG_MAX_ID_LEN);
        p.screen = nullptr;
        p.built = false;
        p.cfgIdx = i;

        if (strcmp(p.id, defaultId) == 0) {
            buildPage(s_pageCount, dash.pages[i]);
        }
        s_pageCount++;
    }

    if (s_pageCount == 0 || s_pages[0].screen) {
        return;
    }
    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (!s_pages[i].screen) {
            buildPage(i, dash.pages[s_pages[i].cfgIdx]);
            return;
        }
    }
}

void reapplyThemeAllPages() {
    s_rebuildRequested = false;

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    if (!dash.loaded || s_pageCount == 0)
        return;

    lv_obj_t *blank = lv_obj_create(nullptr);
    if (!blank) {
        LOG_ERROR("UI", "Theme rebuild: placeholder alloc failed — keeping the current palette");
        return;
    }
    lv_obj_set_style_bg_color(blank, lv_color_hex(ThemeTokens::kGroundNight), LV_PART_MAIN);
    lv_scr_load(blank);

    const uint8_t target =
        s_pendingLazyBuildIdx < s_pageCount ? s_pendingLazyBuildIdx : s_currentIdx;

    for (uint8_t i = 0; i < s_pageCount; ++i) {
        releasePage(s_pages[i]);
    }
    s_pendingFreeIdx = 0xFF;
    s_pendingLazyBuildIdx = 0xFF;

    buildPage(target, dash.pages[s_pages[target].cfgIdx]);
    if (!s_pages[target].screen) {
        LOG_ERROR("UI", "Theme rebuild: page '%s' failed to build", s_pages[target].id);
        return;
    }
    lv_scr_load_anim(s_pages[target].screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    s_currentIdx = target;

    TopBar::reapplyTheme();
    ErrorBar::reapplyTheme();
    BusSilentLine::reapplyTheme();
    DiagDrawer::reapplyTheme();
}

} // namespace PageManagerInternal

void PageManager::reloadFromStorage() {
    using namespace PageManagerInternal;

    LVGL_ASSERT_LOCKED();

    lv_obj_t *blank = lv_obj_create(nullptr);
    if (!blank) {
        LOG_ERROR("UI", "Config reload: placeholder alloc failed — keeping the running pages");
        return;
    }
    lv_obj_set_style_bg_color(blank, lv_color_hex(ThemeTokens::kGroundNight), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(blank, LV_OPA_COVER, LV_PART_MAIN);
    lv_scr_load(blank);

    for (uint8_t i = 0; i < s_pageCount; ++i) {
        releasePage(s_pages[i]);
    }
    s_pageCount = 0;
    s_pendingFreeIdx = 0xFF;
    s_pendingLazyBuildIdx = 0xFF;

    if (!ConfigLoader::reloadAll()) {
        LOG_ERROR("UI", "Config reload: dashboard.json unusable — showing the setup screen");
        SetupScreen::show();
        return;
    }

    ThemeManager::init();
    TopBar::rebuild();
    buildPageList();

    const uint8_t target = defaultPageIndex();
    if (target >= s_pageCount || !s_pages[target].screen) {
        LOG_ERROR("UI", "Config reload: no page could be built");
        return;
    }

    const CfgDashboard &dash = ConfigLoader::getDashboardConfig();
    const CfgPage &page = dash.pages[s_pages[target].cfgIdx];
    TopBar::applyPage(page);
    lv_scr_load_anim(s_pages[target].screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
    s_currentIdx = target;

    BurnOverlay::hide();
    LOG_INFO("UI", "Config reloaded live: %u page(s), showing '%s'",
             static_cast<unsigned>(s_pageCount), s_pages[target].id);
}
