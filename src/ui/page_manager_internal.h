#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "app_config.h"
#include "config/config_types.h"

namespace PageManagerInternal {

static constexpr uint8_t MAX_PAGES = CONFIG_MAX_PAGES;

struct Page {
    char id[CFG_MAX_ID_LEN];
    lv_obj_t *screen;
    bool built;
    uint8_t cfgIdx;
};

extern Page s_pages[MAX_PAGES];
extern uint8_t s_pageCount;
extern uint8_t s_currentIdx;
extern bool s_rebuildRequested;

extern uint8_t s_pendingFreeIdx;

extern uint8_t s_pendingLazyBuildIdx;

void buildPage(uint8_t idx, const CfgPage &cfg);

[[nodiscard]] bool pageDeclaresShiftStrip(const CfgPage &cfg);

void reapplyThemeAllPages();

void buildPageList();

uint8_t defaultPageIndex();

void releasePage(Page &p);

void showPage(uint8_t idx);

void onSwipe(lv_dir_t dir);

} // namespace PageManagerInternal
