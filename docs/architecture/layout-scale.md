# Layout scale

Source: [`include/layout_scale.h`](../../include/layout_scale.h) ·
[`include/display_tiers.h`](../../include/display_tiers.h)

UI code is written once, against a fixed **320×240 design space**, and scaled to
the panel the runtime board profile describes. A new board with a different
resolution needs no UI change.

## The scale functions

| Call                     | Use it for                                         | Scales by              |
| ------------------------ | -------------------------------------------------- | ---------------------- |
| `LayoutScale::x(v)`      | Horizontal extents: x, widths                      | panel width / 320      |
| `LayoutScale::y(v)`      | Vertical extents: y, heights                       | panel height / 240     |
| `LayoutScale::square(v)` | Radii and anything that must keep its aspect ratio | the smaller of the two |

The arithmetic is `layout_scale_rs` in the `layout-grid` Rust crate. The panel size
comes from `canshift::display::width()` / `height()`, which read the **runtime**
board profile, so a profile provisioned over USB rescales the UI on its next boot.

Two things stay unscaled on purpose:

- **Thin border strokes.** A 1 px border scaled up blurs the edge; widgets set
  border widths as literals.
- **Text.** Font sizes come from the display tier's face ladder, not from
  `LayoutScale`. Wrap container geometry, never text metrics.

## Display tiers

A tier is a design space, a layout grid and a font ladder. `display_tiers.h`
declares three:

| Tier     | Design space | Grid    | Widgets per page |
| -------- | ------------ | ------- | ---------------- |
| `base`   | 320×240      | 12 × 12 | 12               |
| `medium` | 480×320      | 16 × 14 | 18               |
| `large`  | 800×480      | 24 × 20 | 28               |

`tierForPanel()` picks the largest tier the panel can hold. Every shipped board
uses `base`: the fonts compiled into the firmware are the base ladder, and
`static_assert`s in `font_manager.cpp` and `widget_factory.cpp` hold the firmware
to it. A larger tier needs its own faces before a board can use it. The simulator
can already render the other two ([`sim/README.md`](../../sim/README.md)).

The tuner previews from the same catalogue in `@canshift/core`
(`src/display-tiers.ts`). `scripts/check_display_tiers.py` fails when the two
disagree. It needs a sibling `../canshift-core` checkout: run it locally before a
tier change, because the `lint` job checks out this repo alone and skips the
comparison.
