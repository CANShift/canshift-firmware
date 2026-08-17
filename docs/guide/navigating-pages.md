# Navigating pages

The dash supports up to **5 pages** in a horizontal carousel. You navigate
between them with swipe gestures.

The tuner's **Live data** route mirrors what the dash is currently showing —
useful to verify a page reads the right signals before you burn.

## Gestures

| Gesture                            | Action                 |
| ---------------------------------- | ---------------------- |
| Swipe **left** anywhere on a page  | Next page              |
| Swipe **right** anywhere on a page | Previous page          |
| Swipe **down** from the top edge   | Open Settings panel    |
| Swipe **up** from the bottom edge  | Expand the Diag drawer |

Swipes that start on a button widget are filtered — a finger drift across a
button does not navigate. This avoids the "I tried to tap and ended up on the
wrong page" frustration.

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

## The cap is 5 pages

The firmware ships with `CONFIG_MAX_PAGES = 5` (#1360). The tuner enforces the
same cap at burn time. Bumping the cap requires a firmware build flag change
and increases BSS by ~6.2 KB per added page — not user-configurable.
