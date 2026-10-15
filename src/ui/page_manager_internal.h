#pragma once

#include <lvgl.h>
#include <stdint.h>

#include "app_config.h"
#include "config/config_types.h"

namespace PageManagerInternal {

static constexpr uint8_t kMaxPages = kConfigMaxPages;

struct Page {
    char id[kCfgMaxIdLen];
    lv_obj_t *screen;
    bool built;
    uint8_t cfgIdx;
};

extern Page s_pages[kMaxPages];
extern uint8_t s_pageCount;
extern uint8_t s_currentIdx;
extern bool s_rebuildRequested;

extern uint8_t s_pendingFreeIdx;

extern uint8_t s_pendingLazyBuildIdx;

extern bool s_keepAllPages;

void showConfigFailure();

[[nodiscard]] bool buildPage(uint8_t idx, const CfgPage &cfg);

[[nodiscard]] bool pageDeclaresShiftStrip(const CfgPage &cfg);

[[nodiscard]] int16_t contentTopY(const CfgPage &cfg);

void reapplyThemeAllPages();

void buildPageList();

void buildPagesAtBoot(uint8_t target);

void rebuildPagesForTheme(uint8_t target);

uint8_t defaultPageIndex();

void releasePage(Page &p);

void releaseAllPages();

void showPage(uint8_t idx);

void onSwipe(lv_dir_t dir);

} // namespace PageManagerInternal
