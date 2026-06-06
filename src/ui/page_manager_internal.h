#pragma once
// page_manager_internal.h — Cross-module forward decls for the dashboard page
// manager. Carved out of page_manager.cpp during the #1207 refactor. Three
// translation units share this header:
//
//   - page_manager.cpp         — orchestrator + public API + state machine
//                                (showPage, navigateTo, updateWidgets, request*)
//   - page_manager_builder.cpp — LVGL page construction, day/night theme
//                                rebuild, cruise-control template
//   - page_manager_anim.cpp    — page transition / lazy build pump,
//                                rebuildAllPages dummy-screen lifecycle (#1295)
//
// All translation units assume the LVGL mutex is held when they touch LVGL
// primitives, matching the contract of the public PageManager API.

#include <lvgl.h>
#include <stdint.h>

#include "app_config.h"
#include "config/config_types.h"

namespace PageManagerInternal {

// ---------------------------------------------------------------------------
// Page slot — one per visible dashboard page. `screen == nullptr` means the
// page is registered but not yet built (lazy construction on first visit).
// ---------------------------------------------------------------------------

static constexpr uint8_t MAX_PAGES = CONFIG_MAX_PAGES;

struct Page {
    char id[CFG_MAX_ID_LEN];
    lv_obj_t *screen; // LVGL screen object — nullptr until first navigation (lazy build)
    bool built;
    uint8_t cfgIdx; // index into CfgDashboard::pages[] for lazy build
};

// ---------------------------------------------------------------------------
// Swipe-transition duration (ms). Kept at 120 ms (down from PR #59's 180 ms in
// fix #331/F4) because the previous value dominated page-switch wall time.
// Shared between the orchestrator (navigateNext/navigatePrev) and the anim
// module (onSwipe).
// ---------------------------------------------------------------------------

static constexpr uint32_t SWIPE_ANIM_MS = 120;

// ---------------------------------------------------------------------------
// Shared mutable state — defined in page_manager.cpp (the orchestrator owns
// the state machine's lifecycle). Helper TUs reach in via these extern decls,
// matching the ownership pattern from settings_page_internal.h / #1321.
// ---------------------------------------------------------------------------

extern Page s_pages[MAX_PAGES];
extern uint8_t s_pageCount;
extern uint8_t s_currentIdx;
extern lv_obj_t *s_revOverlay;
extern bool s_rebuildRequested;
extern bool s_reloadRequested;

// Page scheduled for pool release after the in-flight animation completes.
// 0xFF = nothing pending. Consumed at the start of the next showPage() call so
// the departing screen stays alive for the full animation window before its
// LVGL objects are deleted.
extern uint8_t s_pendingFreeIdx;

// Deferred lazy-build state — set by showPage(), consumed by asyncDoLazyBuild().
// Using lv_async_call defers the lv_obj_del to the next lv_timer_handler()
// iteration so it never fires from within an event callback.
extern uint8_t s_pendingLazyBuildIdx;
extern uint32_t s_pendingLazyBuildMs;

// ---------------------------------------------------------------------------
// Builder module — LVGL page construction + theme rebuild
// (page_manager_builder.cpp)
// ---------------------------------------------------------------------------

// Build one page's LVGL widget tree from its CfgPage. Sets s_pages[idx].screen
// on success, leaves it nullptr on failure (heap exhaustion, font load, ...).
void buildPage(uint8_t idx, const CfgPage &cfg);

// Theme-toggle fast path (#1257) — walks every built page and re-applies the
// new day/night palette in place without rebuilding any widgets.
void reapplyThemeAllPages();

// Destructive rebuild path used by USB CMD_PUT_CONFIG (#1295 guards the
// dummy-screen UAF). Drops every page, rebuilds the previously active one
// eagerly, and falls back to the first page that survives if that fails.
void rebuildAllPages();

// ---------------------------------------------------------------------------
// Anim module — page transition + lazy build pump
// (page_manager_anim.cpp)
// ---------------------------------------------------------------------------

// Navigate to a page by index with the supplied transition. Handles lazy
// build via lv_async_call (asyncDoLazyBuild) when the target page has not
// been built yet, and schedules the departing page for pool release on the
// next call.
void showPage(uint8_t idx, lv_scr_load_anim_t anim, uint32_t durationMs);

// Convenience overload used by navigateTo — defaults to FADE_IN, 120 ms.
void showPage(uint8_t idx);

// Swipe gesture callback, registered with GestureController::setSwipeHandler()
// from PageManager::init().
void onSwipe(lv_dir_t dir);

} // namespace PageManagerInternal
