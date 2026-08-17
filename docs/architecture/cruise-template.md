# Cruise template

Source: [`src/ui/widgets/cruise_control_widget.cpp`](../../src/ui/widgets/cruise_control_widget.cpp)

The cruise control page in `page_manager_builder.cpp` renders four buttons
in an L-shape around a centred set-speed readout. The geometry is
non-trivial because LVGL 8.4's `lv_draw_polygon` is convex-only — each
button is a non-convex shape (the inner corner is notched). This file
documents the workarounds.

## The visual

```
┌────────────┐  ┌────────────┐
│            │  │            │
│     −      │  │     +      │
│         ┌──┤  ├──┐         │
│         │  │  │  │         │
└─────────┘  │  │  └─────────┘
             │  │
            (SET-SPEED label)
             │  │
┌─────────┐  │  │  ┌─────────┐
│         │  │  │  │         │
│         └──┤  ├──┘         │
│    SET     │  │    OFF     │
│            │  │            │
└────────────┘  └────────────┘
```

Each button is two axis-aligned rectangles glued together with a notched
inner corner. The outer five corners are rounded; the notch's two corners
are rounded at smaller radius. Studio's `CruiseControlPreview.tsx` is the
SoT for layout; firmware mirrors it pixel-by-pixel.

## Why polygons, not lv_btn

LVGL's button widget is rectangle-only. Drawing the L-shape needs
custom polygon paint. The implementation:

- Strip every theme style via `lv_obj_remove_style_all(btn)` to keep the
  theme from re-introducing a rectangle background under the L-shape.
- Hook `LV_EVENT_DRAW_MAIN_END` with a custom callback
  (`cruiseLDrawCb`) that renders the L-shape as **two axis-aligned
  rectangles** (the convex pieces) + a **polyline outline** (the
  rounded contour).
- Hook `LV_EVENT_HIT_TEST` with `cruiseLHitTestCb` that rejects clicks
  inside the notch — without it, the notch area would route taps to
  the button underneath.

## Bezier-approximated corners

The outline is a polyline whose corners are quadratic-Bezier approximations
of arcs. `cruiseBezierAt(x0, y0, xc, yc, x2, y2, t)` evaluates the curve at
parameter `t`; `cruiseEmitBezier` samples it at `CRUISE_BEZIER_SEGS = 4`
points (skipping `i=0` because the previous polyline endpoint already sits
there). Four segments per corner is the lowest count that still reads as
smooth against the 320×240 panel.

The notch corners use a smaller radius (`CRUISE_INNER_R = 5`) than the
outer rounded corners (`CRUISE_BUTTON_RADIUS = 8`) so the inset reads as
intentional rather than a draft artifact.

Per-corner build functions live in
`buildCruiseLPath_{TL,TR,BL,BR}` — one per which corner carries the notch.
The enum is prefixed with `k` (`CruiseCorner::kTL` etc.) because xtensa's
`specreg.h` defines `TR`, `BR` as macros and the unprefixed names would
collide at preprocess time.

## Theme override workaround for the labels

The button labels (`+`, `−`, `SET`, `OFF`) and the SET-SPEED readout
ride on the parent `screen`, not the button. Reason: LVGL's default
theme observer subscribes to `text_color` change events on any object
whose parent is a button, and overwrites the colour we set. Moving the
labels to be siblings of the buttons (children of `screen`) escapes
the observer.

The labels use bigger fonts than the rest of the dashboard:

- `+` and `−` at `FontManager::primary(32)`
- `SET` and `OFF` at `FontManager::secondary(24)`

## State-aware visual

- Idle: dark fill (`CRUISE_BUTTON_FILL_RGB = 0x1A1A1A`), red 2-px stroke
  (`CRUISE_BUTTON_STROKE_RGB = 0xE03030`).
- Pressed: lighter fill (`CRUISE_BUTTON_FILL_PRESSED_RGB = 0x3A3A3A`),
  same stroke.

The fill colour is recomputed inside `cruiseLDrawCb` based on
`lv_obj_get_state(btn) & LV_STATE_PRESSED`. Setting the fill via theme
styles would re-introduce the rectangular background and defeat the
custom polygon path.

## ON/OFF toggle on BR button

The BR (`OFF`) button is bound to the cruise active flag; on click it
toggles `s_cruiseActive` and re-renders the centre label
(`cruiseSyncToggleVisual`). When inactive the SET-SPEED placeholder is
`0`; when active it tracks `SignalIds::CRUISE_SETPOINT_KPH` from the
ECU (or the ECHO from the local press if the bus is silent — see the
`button-widget` optimistic-write doc).

## Why not lv_btnmatrix?

`lv_btnmatrix` has a per-cell border + label model but is still
rectangle-bound. Building the L-shape on top would still need the
custom polygon paint; the savings on the label/event routing are
not worth the extra complexity.

## Layout constants

```
CRUISE_BUTTON_W      140
CRUISE_BUTTON_H      85
CRUISE_GAP_X         12
CRUISE_GAP_Y         10
CRUISE_OUTER_PAD     8
CRUISE_CENTER_W      100
CRUISE_CENTER_H      76
CRUISE_NOTCH_MARGIN  6
CRUISE_NOTCH_W       CRUISE_CENTER_W / 2 + CRUISE_NOTCH_MARGIN
CRUISE_NOTCH_H       CRUISE_CENTER_H / 2 + CRUISE_NOTCH_MARGIN
CRUISE_BUTTON_RADIUS 8     // outer rounded corners
CRUISE_INNER_R       5     // notch corners (smaller)
CRUISE_BUTTON_BORDER_W 2
CRUISE_BEZIER_SEGS   4     // segments per corner
CRUISE_L_MAX_PTS     40    // polyline buffer cap
```

Bumping any of these requires re-checking
`canshift-tuner/src/components/editor/CruiseControlPreview.tsx` so the
preview stays pixel-faithful with the firmware.

## Known limitations

- The convex notch corners aren't perfectly rounded (vs the outer
  rounded corners). The current overlay approach hit its ceiling
  there — phase 2 of #1375 tracks the fix.
- Holding the LVGL mutex for the polygon paint is implicit (the draw
  callback runs from `lv_task_handler`). No special locking needed.
