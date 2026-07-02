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

static constexpr uint32_t SWIPE_ANIM_MS = 120;

static constexpr lv_coord_t REVLIMIT_BORDER_WIDTH_PX = 8;
static constexpr lv_opa_t REVLIMIT_BORDER_DIM_OPA = LV_OPA_50;

extern Page s_pages[MAX_PAGES];
extern uint8_t s_pageCount;
extern uint8_t s_currentIdx;
extern lv_obj_t *s_revOverlay;
extern bool s_rebuildRequested;

extern uint8_t s_pendingFreeIdx;

extern uint8_t s_pendingLazyBuildIdx;
extern uint32_t s_pendingLazyBuildMs;

void buildPage(uint8_t idx, const CfgPage &cfg);

void reapplyThemeAllPages();

void showPage(uint8_t idx, lv_scr_load_anim_t anim, uint32_t durationMs);

void showPage(uint8_t idx);

void onSwipe(lv_dir_t dir);

} // namespace PageManagerInternal
