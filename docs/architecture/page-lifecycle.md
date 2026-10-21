# Page lifecycle

Source: [`src/ui/page_manager.cpp`](../../src/ui/page_manager.cpp)

PageManager owns the dashboard's page tree, the LVGL screens it represents,
and the swipe/transition state machine. Memory pressure on a no-PSRAM WROOM
makes this the most fragile part of the UI. Sources:
`src/ui/page_manager.cpp`, `src/ui/page_manager_anim.cpp`,
`src/ui/page_manager_builder.cpp`, `src/ui/widget_factory.cpp`.

## Every page is built at boot

`buildPagesAtBoot()` (`page_manager_residency.cpp`) builds every visible page
before the dash starts. A page whose widgets do not all build logs an error
and pushes `PAGE_BUILD` with the page id to the error store, so a broken
layout shows at boot instead of on the first swipe to it.

What happens next depends on the board:

| Board                   | Boot                                                                                                                                                     | Swipe                                                             |
| ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| PSRAM (S3)              | The widget tag pool grows into PSRAM (`kPoolSlots × kMaxPages` slots), every page is built and kept, and the fonts are preloaded                         | Loads a screen that already exists: nothing is allocated or freed |
| PSRAM, pages do not fit | All-or-nothing: if any page is incomplete, or the LVGL pool keeps less than 64 KB free, every page is released and the board falls back to the row below | Lazy, as below                                                    |
| No PSRAM (WROOM)        | Each page is built alone, checked, and released, then the default page is built. Peak use is one page, as on a swipe                                     | Lazy build of the target page, as below                           |

The fallback keeps one page alive because the tag pool holds
`CONFIG_MAX_WIDGETS_PER_PAGE` tags. On a WROOM the boot check still loads
every font face some page uses, so no face is loaded for the first time
while driving.

## Lazy build without PSRAM

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

With every page resident, nothing is released. Otherwise, LVGL screen transitions need both screens alive while the animation
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
`kSwipeCancelThresholdPx = 8 px`, the indev is reset
(`lv_indev_reset_long_press` + `lv_indev_reset`) and the swipe is
dispatched directly. Waiting for LVGL's own cumulative `gesture_limit`
left a 12–40 px dead zone where the click was cancelled but no swipe
fired — the page sat still and the user thought the swipe was lost
(#1262).

## Rev-limiter overlay layer

`s_revOverlay` is a red translucent rectangle on `lv_layer_top()` that
sits above pages but below ErrorBar / Settings. It is
visibility-toggled by `AlertEngine::isRevLimiterFlashOn()` from
`updateWidgets()`. Mounting on `lv_layer_top` means it survives
page transitions (the layer is shared across screens), so the alert flash
keeps blinking through a swipe.

## Alerts run on every frame

`PageManager::tickAlerts()` runs on every UI frame before the dash/workshop
split: stale invalidation, `AlertEngine::tick` and `AlertTakeover::update`. It
does not depend on a page existing, so a critical alert still raises the
takeover, the tone and the BLE `ALERT` frame in workshop mode, during an OTA and
after a reload that left no page. Widget and chrome refresh stay in
`updateWidgets()`, which skips all of that. The takeover is re-raised to the
front of `lv_layer_top` on every update while it is active, so it covers the
workshop card and the OTA overlay, and comes back after `releaseAll()` hid it.

## Page and widget budgets

`CONFIG_MAX_PAGES` is 8 and `CONFIG_MAX_WIDGETS_PER_PAGE` is 12
([`include/app_config.h`](../../include/app_config.h)). Core mirrors both in
`FIRMWARE_CAPS`, so the tuner refuses to burn a dashboard that exceeds them.

The `CfgPage` array itself is static — 8 slots, always resident. What is _not_
static is the widget storage: each page's `widgets` array is heap-allocated at
load time, sized to the page's actual widget count. That is what keeps a
four-page dashboard from paying for twelve widgets on eight pages.

The allocation is bounded. `kConfigDashboardHeapBudgetBytes` (48 KB) caps
the cumulative widget allocation across all pages, checked page by page as the
config is parsed. Crossing it pushes a `BUDGET` error and **refuses the
payload** rather than allocating until something else fails — a config that is
too large is rejected at load, not discovered as an OOM during CAN init or USB
buffer reservation later in boot.

A page whose widget count exceeds `CONFIG_MAX_WIDGETS_PER_PAGE` is clamped with
a warning; the extras are ignored rather than rejecting the whole file.

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

## Timer widgets

A `timer` widget carries a `source` that decides what it reads and whether it is
interactive:

| Source              | Reads (track telemetry live / otherwise)                  | Renders                                                                |
| ------------------- | --------------------------------------------------------- | ---------------------------------------------------------------------- |
| `elapsed` (default) | `TimerService`                                            | the stopwatch, `MM:SS` or `SS.mmm` — the only touch-interactive source |
| `lap`               | `TrackStore.currentLapMs` / the stopwatch's running split | `M:SS.hh`                                                              |
| `best` / `last`     | `TrackStore` / the stopwatch's best and last lap          | `M:SS.hh`, or `--:--` when unset                                       |
| `lapCount`          | `TrackStore.lapNumber` / the stopwatch's lap count        | an integer                                                             |
| `delta`             | `TrackStore.deltaMs` / nothing                            | `-0.42` / `+0.31`, or `--` without track telemetry                     |

`TimerSources::select` picks one side for every field at once, so a widget never
mixes a phone lap with a stopwatch lap.

**Track telemetry is not on the bus.** `TrackStore` is fed by the companion app
through `applyTrackState`. `kTrackTelemetryTimeoutMs` decides when that data has
gone stale; past it, or with no phone at all, every split comes from the on-device
stopwatch: `TimerCore` keeps the last and best lap and the running split since the
last lap press, and clears them on reset and on a new session. `delta` blanks
out, because the stopwatch has no reference lap to predict against and a delta
against a stale one is worse than no delta.

Only `elapsed` binds touch handlers. The other sources are readouts — they carry no
state border and no lap badge, and the timer is driven from buttons instead.

Timer widgets update on every UI tick regardless of signal binding. `updateWidget`
returns early for a widget with no bound signal, which is right for a gauge and
wrong for a timer: before this branch existed the stopwatch label never refreshed
after its initial `00:00`.

## Display idle

A dash wired to switched ignition stays powered when the ECU is not. Rather than
hold full brightness on an empty bus, the panel steps down:

| State | After       | Backlight                     |
| ----- | ----------- | ----------------------------- |
| live  | —           | the user's brightness setting |
| dim   | 60 s idle   | 20 % of it                    |
| off   | 10 min idle | 0                             |

**Idle is the time since the more recent of a CAN frame and a touch** —
`min(CanManager::msSinceLastRx(), lv_disp_get_inactive_time())`. So a live bus keeps
the dash lit with nobody touching it, and a hand keeps it lit on a dead bus. The
first frame or the first touch restores brightness in one tick.

The state machine is `display_idle_state` in `rust/alert-engine`, next to the
bus-silence logic it belongs with. A zero threshold disables that step, so a
configuration can dim without ever blanking.

Only the panel dims. USB and BLE keep answering, the CAN task keeps parsing, and
alerts keep evaluating. **A critical takeover is a wake source of its own**: while
`AlertTakeover::isActive()` the state is held at live, so a critical alert relights
a blanked panel even with no frame and no touch to reset the idle clock.

The touch that wakes a blanked panel is swallowed rather than delivered, so waking
the dash cannot also press the button under your finger. The suppression targets
the pointer indev directly — `lv_indev_get_act()` is only set inside LVGL's read
callback, so it cannot be used from the idle path.

### Power while dark

A blanked panel also stops paying for what nobody can see:

| While off          | Effect                                                        |
| ------------------ | ------------------------------------------------------------- |
| LVGL refresh timer | paused — widgets and alerts still update, nothing is rendered |
| CPU clock          | 80 MHz instead of the board's nominal clock (`CpuClock`)      |
| Touch read period  | 100 ms instead of 15 ms                                       |

The APB clock stays at 80 MHz at either CPU speed, so TWAI, SPI, I2C, UART and the
backlight PWM keep their timings; the CAN task on the other core keeps running,
only slower, until the first frame wakes the panel.

Waking reverses it in order: nominal clock first, then the refresh timer resumes,
the whole screen is invalidated and rendered with `lv_refr_now`, and only then
the backlight comes back — the stale frame from ten minutes ago is never lit.

Anything that must be seen while dark calls `DisplayIdle::wake()` before it draws:
the BLE passkey card, the burn overlay, the OTA overlay and workshop-mode entry.

A live panel nobody has touched for 3 s reads touch every 33 ms instead of 15 ms;
the first press brings the rate back. The periods are `touch_read_period_ms` in
`rust/alert-engine/src/display_power.rs`; the active one is pinned to
`LV_INDEV_DEF_READ_PERIOD` by a `static_assert`.

### Not the same as stale values

Three different things, deliberately distinct:

- a **signal** going quiet renders `- -` in Dim after its `timeoutMs`;
- the **bus** going quiet raises the bus-silent line;
- the **dash** going idle dims and eventually blanks.

A dash showing `- -` everywhere is saying "I am here, the data is not". A blank dash
is saying "nothing has happened for ten minutes". They should not be confused, which
is why the idle path drives the backlight and never the content.
