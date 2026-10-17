# Navigating pages

The dash supports up to **5 pages** in a horizontal carousel. You navigate
between them with swipe gestures or by tapping the screen edges.

The tuner's **Live data** route mirrors what the dash is currently showing —
useful to verify a page reads the right signals before you burn.

## Gestures

| Gesture                               | Action              |
| ------------------------------------- | ------------------- |
| Swipe **left** anywhere on a page     | Next page           |
| Swipe **right** anywhere on a page    | Previous page       |
| Tap the **right third** of the screen | Next page           |
| Tap the **left third** of the screen  | Previous page       |
| Swipe **down** from the top edge      | Open Settings panel |

A tap in the middle third does nothing. Both the swipe and the tap wrap from
the last page back to the first, and the other way round.

Swipes and taps that start on a button widget are filtered — a finger drift
across a button does not navigate, and a tap on a button, the timer or an
alert keeps its own action. This avoids the "I tried to tap and ended up on
the wrong page" frustration. Taps never turn the page while Settings is open,
from the top bar, during an alert takeover or a control splash, while a
config or update is being written, or in workshop mode.

## Steering-wheel buttons

`dashboard.json` can bind two CAN signals to page turns:

```json
"pageButtons": { "next": "<signal>", "previous": "<signal>" }
```

Both keys are optional. A press is the signal going from `0` to any non-zero
value. A second press within 150 ms is ignored as a bounce. A stale or absent
signal does nothing, and the signal has to read `0` again before the next
press counts. Button presses turn the page the same way a swipe does, but not
during an alert takeover, a control splash, a config or update write, or in
workshop mode.

The signal has to be one the dash reads (the signal catalog in
`rust/signal-map`). A name outside it is logged and the button stays unbound.

## The default page

The dash boots into the page whose `id` matches `dashboard.json`'s
`defaultPageId`. If that page doesn't exist (typo, removed page), it falls
back to the first visible page.

Set the default in the tuner: page settings → "Default on boot".

## Hidden pages

A page with `visible: false` in `dashboard.json` is not built and not in the
carousel — useful for staging a new page that's not ready for the driver yet.

## Lazy build — why a fresh page hesitates

Only the default page is built eagerly. Other pages build on first navigation,
which takes ~50–150 ms depending on widget density. The first swipe to a new
page may show a brief blank frame before the transition completes; subsequent
swipes are instant because the page stays built until you navigate away.

Pages are released on the **next** navigation (not immediately when you leave),
to keep the transition animation smooth. Details in
[Page lifecycle](../architecture/page-lifecycle.md).

## The cap is 8 pages

The firmware ships with `CONFIG_MAX_PAGES = 8`, mirrored by core's
`FIRMWARE_CAPS.MAX_PAGES`, so the tuner enforces the same cap at burn time. It
is a compile-time constant, not a user setting.

Per-page widget storage is heap-allocated within a cumulative budget, so a
four-page dashboard does not pay for eight — but the page array itself is
static at eight slots.
