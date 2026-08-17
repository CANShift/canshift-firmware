# Page lifecycle

Source: [`src/ui/page_manager.cpp`](../../src/ui/page_manager.cpp)

PageManager owns the dashboard's page tree, the LVGL screens it represents,
and the swipe/transition state machine. Memory pressure on a no-PSRAM WROOM
makes this the most fragile part of the UI. Sources:
`src/ui/page_manager.cpp`, `src/ui/page_manager_anim.cpp`,
`src/ui/page_manager_builder.cpp`, `src/ui/widget_factory.cpp`.

## Lazy build, not eager

Only the default page is built eagerly at boot. Other pages stay as
`Page{built=false, screen=nullptr}` entries until first navigation. The LVGL
pool would OOM on gauge-heavy configs if every page were materialised at
boot — the pool runs ~80 KB and a single dense page can claim 20 KB+ in
arc + label objects.

When a swipe / `navigateTo` lands on an unbuilt page, `showPage()` schedules
the build via `lv_async_call(asyncDoLazyBuild, nullptr)` instead of doing it
inline. The async callback runs at the start of the next
`lv_timer_handler()` iteration, outside any LVGL event callback. This
avoids the use-after-free that occurs when `lv_obj_del(dep.screen)` is
called synchronously from within a button click handler: `lv_obj_del`
fires `LV_EVENT_DELETE` on the button, which frees its tag, and the click
handler then continues with a dangling pointer.

If a second lazy-build request lands while the first is still pending,
`s_pendingLazyBuildIdx` is overwritten (latest-wins debounce) and the
second `lv_async_call` is skipped — the queued handler runs once and uses
the most recent index. This matches typical UI debounce semantics.

## Release on the next navigation

LVGL screen transitions need both screens alive while the animation
plays. `showPage()` defers the release of the departing page until the
**next** call: `s_pendingFreeIdx = s_currentIdx` after starting the
animation, and the head of the next `showPage()` invocation
(`s_pendingFreeIdx < s_pageCount && s_pendingFreeIdx != idx`) actually
frees it via `WidgetFactory::clearAll` + `lv_obj_del`. Skipping the
release when the user navigated back to the very page that was scheduled
for release is intentional — the page is now the current page again.

## Lazy-build dummy screen UAF guard (#1295)

When `asyncDoLazyBuild` runs after a swipe-back, the screen the user is
swiping toward may have been released between the swipe and the async
callback. The callback creates a blank "placeholder" screen, swaps it in
via `lv_scr_load`, releases the old screen if it's still alive, then
finally builds the target page and runs `lv_scr_load_anim` with
`placeholderActive=true`. This pattern was the fix for #1295.

## Swipe arbitration

`onSwipe(dir)` filters when the press object was clickable
(`lv_indev_get_obj_act() + LV_OBJ_FLAG_CLICKABLE`). The user has tapped
a button — a swipe that incidentally crosses the button's bounds should
NOT navigate. Without this guard, dragging a finger across a button-filled
page mid-tap navigated unexpectedly.

`gesture_controller.cpp` also drives an early swipe via
`cancelClickIfSwiping`: once horizontal travel passes
`SWIPE_CANCEL_THRESHOLD_PX = 8 px`, the indev is reset
(`lv_indev_reset_long_press` + `lv_indev_reset`) and the swipe is
dispatched directly. Waiting for LVGL's own cumulative `gesture_limit`
left a 12–40 px dead zone where the click was cancelled but no swipe
fired — the page sat still and the user thought the swipe was lost
(#1262).

## Rev-limiter overlay layer

`s_revOverlay` is a red translucent rectangle on `lv_layer_top()` that
sits above pages but below ErrorBar / DiagDrawer / Settings. It is
visibility-toggled by `AlertEngine::isRevLimiterFlashOn()` from
`updateWidgets()`. Mounting on `lv_layer_top` means it survives
page transitions (the layer is shared across screens), so the alert flash
keeps blinking through a swipe.

## `MAX_PAGES = 5`

The cap evolved through several iterations and the current value is
load-bearing:

- **4 (pre-#1351)**: too tight for cruise_control + 4 free-form pages.
- **8 (#1357)**: jumped +25 KB BSS, OOM'd CAN init + USB rxBuf at boot
  (#1358 revert).
- **5 (now, #1360)**: minimal bump that fits the demo seed (4 pages) +
  1 cruise. Costs +6.2 KB BSS vs the 4-page baseline — well inside the
  post-#1351 WiFi-removal headroom (~80 KB). The 25 KB jump from 4→8 was
  what hit fragmentation; +6.2 KB is comfortably below the cliff.

Heap-allocating the page array (#1359) is the durable fix and would
retire this static cap entirely.

## Widget tag pool

Every widget that needs per-instance state (gauge cache, button latch,
warning blink state, etc.) backs that state with a slot from
`WidgetTagPool`. The pool is sized to
`CFG_MAX_WIDGETS_PER_PAGE` slots — one page is bounded to that many
widgets, so the pool can never be exhausted during a single page build.
When a widget is destroyed, `LV_EVENT_DELETE` fires
`WidgetTagPool::deleteHandler<Tag>` which releases the slot.

`WidgetTagPool::Slot<Tag>` is an RAII guard used at widget-create sites:
if the create() function returns early before `lv_obj_add_event_cb`
attaches `deleteHandler`, the guard's destructor releases the slot
deterministically. After `tagSlot.commit()` LVGL owns destruction.
