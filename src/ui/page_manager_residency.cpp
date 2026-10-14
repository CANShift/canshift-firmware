#include "page_manager_internal.h"

#include "config/config_loader.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "diag/lvgl_pool.h"
#include "hal/memory/psram.h"
#include "widgets/widget_tag_pool.h"

namespace PageManagerInternal {

bool s_keepAllPages = false;

namespace {

constexpr uint32_t kResidentPoolHeadroomBytes = 64U * 1024U;

const CfgPage &pageConfig(uint8_t idx) {
    return ConfigLoader::getDashboardConfig().pages[s_pages[idx].cfgIdx];
}

void buildAndReport(uint8_t idx) {
    if (buildPage(idx, pageConfig(idx)))
        return;
    LOG_ERROR("UI", "Page '%s' did not build completely — check its widgets", s_pages[idx].id);
    ErrorStore::push(ERROR_SRC_CONFIG, "PAGE_BUILD", s_pages[idx].id);
}

void releaseAll() {
    for (uint8_t i = 0; i < s_pageCount; ++i)
        releasePage(s_pages[i]);
}

void validateEachThenBuild(uint8_t target) {
    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (i == target)
            continue;
        buildAndReport(i);
        releasePage(s_pages[i]);
    }
    buildAndReport(target);
}

bool buildEveryPage() {
    for (uint8_t i = 0; i < s_pageCount; ++i) {
        if (!buildPage(i, pageConfig(i)))
            return false;
    }
    return LvglPool::stats().free >= kResidentPoolHeadroomBytes;
}

bool reserveResidentTags() {
    if (WidgetTagPool::reserveResidentSlots(WidgetTagPool::kPoolSlots * MAX_PAGES))
        return true;
    LOG_WARN("UI", "No PSRAM for %u widget tags — pages build on swipe instead",
             static_cast<unsigned>(WidgetTagPool::kPoolSlots * MAX_PAGES));
    return false;
}

bool keepAllResident() {
    if (!canshift::hal::memory::isPsramAvailable() || !reserveResidentTags())
        return false;
    if (buildEveryPage())
        return true;
    LOG_WARN("UI", "The pages do not all fit the LVGL pool at once — they build on swipe instead");
    releaseAll();
    return false;
}

} // namespace

void buildPagesAtBoot(uint8_t target) {
    s_keepAllPages = keepAllResident();
    if (!s_keepAllPages)
        validateEachThenBuild(target);
    LvglPool::report(s_keepAllPages ? "every page resident" : "every page validated");
}

void rebuildPagesForTheme(uint8_t target) {
    if (s_keepAllPages)
        s_keepAllPages = keepAllResident();
    if (!s_keepAllPages)
        buildAndReport(target);
}

} // namespace PageManagerInternal
