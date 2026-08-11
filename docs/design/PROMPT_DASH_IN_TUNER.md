# Prompt — the dash exactly as the Tuner draws it

For Claude Code in `canshift-firmware`. The authority for this spec is the dash canvas in the middle of
the Tuner (`CANShift Tuner.dc.html`, DASHBOARD → the 320 × 240 canvas shown at 2×). Whatever the Tuner
renders in that canvas is what the panel must render. If the firmware and the canvas disagree, the
firmware is wrong.

---

You are implementing the CANShift dash rendering (LVGL, CrowPanel 2.8″, 320 × 240, landscape) so that it
matches the Tuner's canvas render pixel for pixel. Read this whole spec, audit the current pages against
it, produce a deviation list, then fix in the order given at the end.

## Scale — read this first

The Tuner canvas is authored at **2×**: a 640 × 480 box that stands for the 320 × 240 panel. Every number
below is given in both. **Device px is what you implement.** The one place the halving does not apply is
type: fonts are generated at the device sizes in the table, and no label font goes below 10 px real —
where a naive halving would give less, use 10 px.

## Frame

| | Canvas (2×) | Device |
| --- | --- | --- |
| Panel | 640 × 480 | 320 × 240 |
| Outer padding | 16 px | **8 px**, all four sides |
| Row gap (shift light / status / grid) | 12 px | **6 px** |
| Grid | 12 columns, gap 12 px | 12 columns, gap **6 px** |
| Layout grid step (Tuner snap) | 8 px | 8 px — widget boxes always land on it |

Content stacks top to bottom: shift light (when the page has one) → status row → widget grid. The grid
is `align-content: start` — widgets do not stretch to fill leftover height, the page ends where the
content ends.

## Colour — two themes, nothing else

| Role | NIGHT (default) | DAY |
| --- | --- | --- |
| Ground | `#121212` | `#DDDDDD` |
| Surface (engaged button face) | `#1F1F1F` | `#F0F0F0` |
| Ink (values, rules, outlines) | `#FFFFFF` | `#000000` |
| Dim (kickers, units, status row) | `#BABABA` | `#5A5A5A` |
| Track (bars, unlit cells) | `#222222` | `#C4C4C4` |
| Danger | `#FF4444` | `#FF4444` |
| Engaged accent | `#FF4747` | `#FF4747` |

Danger and engaged are theme-independent. `#FF4444` is the danger value colour and the top of the shift
light; `#FF4747` is the engaged button border/fill and the progress fill. Never a third red, never a
gradient, never a shadow, radius 0 everywhere.

## Type

Two LVGL bitmap fonts, generated with `lv_font_conv`:

- **Archivo 800** — kickers and button labels only. Kicker: 10 px device, tracking 0.18em, UPPERCASE, in
  Dim (or `#FF4444` on a danger widget, white-75 % on an engaged button).
- **Mono (JetBrains Mono)** — every value, the status row, units. **Tabular numerals mandatory** — a
  value must never change width as digits change.

Value sizes, from the Tuner's `big` property:

| Class | Canvas | Device | Tracking | Line-height |
| --- | --- | --- | --- | --- |
| Hero (speed, gear, rpm on Street/Track) | 96 / 88 / 80 | **48 / 44 / 40** | -0.045em | 0.92 |
| Primary (`big ≥ 64`) | 64 | **32** | -0.045em | 0.92 |
| Mid | 48 / 44 | **24 / 22** | -0.03em | 0.92 |
| Secondary (default) | 34 | **17** | -0.03em | 0.92 |
| Unit, inline after the value | 16 | **10** | — | — |
| Status row | 12 | **10** (floor) | 0.1em | — |

The unit follows the value on the same baseline, after one space, in Dim. It is omitted entirely on
gear and on engaged buttons.

## Widget grammar — this is the part that drifts

Three widget kinds, and only three.

### 1. Value widget (the default)

- **No box, no border, no fill.** A widget is a **top rule + kicker + value**, nothing else. The rule is
  what separates it from its neighbour, so never draw a card.
- Top rule: **2 px Ink** when `big ≥ 64` (primary), **1 px Track** otherwise, **2 px `#FF4444`** when the
  widget is in danger.
- Padding inside the widget: top 7 px / bottom 6 px canvas for primaries, 5 / 4 for secondaries
  (**device: 4/3 and 3/2**); right 16 px canvas → **8 px device**; **left 0** — the kicker and the value
  are flush with the column edge.
- Gap between kicker and value: 3 px canvas primaries, 1 px secondaries (**2 px / 1 px device**).
- Danger widget: rule, kicker and value all in `#FF4444`. Nothing else changes — no fill, no icon.
- Optional bar gauge under the value when the signal has a percentage: **3 px canvas → 2 px device**
  high, Track ground, Ink fill (or `#FF4444` in danger), `margin-top: 7px` canvas → **4 px device**,
  right margin matching the widget (8 px device), square ends.

### 2. Engaged button widget (Controls page)

- The only widget that is a box: **2 px border**, min-height 96 canvas → **48 px device**, padding
  12 × 14 canvas → **6 × 7 device**, content vertically centred, still flush left.
- Off: border in Ink, ground = the page ground. On: border **and** ground `#FF4747`, label white-75 %,
  value white.
- Kicker above, state word below (`LAUNCH` / `ARMED`, `MAP` / `MAP 2`). No unit.
- Real touch target never below 48 × 50 px device — widen the span rather than shrink the box.

### 3. Arc gauge (opt-in, rpm/boost only)

- 160-unit square viewport, radius 62, **stroke 26** (canvas) → **13 device**, sweep starts at 135° and
  runs 292 of 390 units of dash length (a 270° sweep).
- Track stroke in Track colour, value stroke in Ink (or `#FF4444`), the numeral centred inside at 42
  canvas → **21 device**, tabular mono. No tick marks, no needle, no outer ring.

## Shift light

Present only on pages that declare it (Track). A single row of **12 equal cells**, height 14 canvas →
**7 px device**, gap 3 canvas → **2 px device**, square. Cells 1–7 Ink, 8–9 `#FF4444`, 10–12 Track when
unlit. The ramp fills left to right; at the limiter the whole row blinks (see Motion).

## Status row

One line under the shift light, mono, Dim, tracking 0.1em, three fields justified left / centre / right:
bus rate (`CAN 842 Hz`), page status (`MAP 1`, `LAP 4`), page-specific right field (`TRIP 128 km`,
`LAP 4 — 1:38.42`, `BEST 1:36.08`). Never more than three fields, never an icon.

## The six pages — spans are binding

Spans are out of 12. Reproduce them exactly; they are what the Tuner writes into the config.

**01 Street** — SPEED 7 (hero 48), GEAR 5 (hero 48), RPM 6 (22 + bar 30 %), WATER 3, FUEL 3 (bar 46 %),
BATT 4 (17), ODO 8 (17). Status: `MAP 1` · `TRIP 128 km`.

**02 Track** (shift light) — RPM 7 (44), GEAR 5 (44), SPEED 6 (32), OIL PRESS 6 (32, **danger**),
WATER 3 (17), OIL T 3 (17), BOOST 3 (17), BATT 3 (17). Status: `MAP 1` · `LAP 4 — 1:38.42`.

**03 Engine** — WATER 6 (32, bar 70 %), OIL TEMP 6 (32, bar 78 %), OIL PRESS 6 (32, **danger**,
bar 22 %), FUEL PRESS 6 (32, bar 63 %), GEARBOX 4 (17), DIFF 4 (17), BATT 4 (17). Status: `MAP 1` ·
`MAX OIL 128`.

**04 Tuning** — BOOST 7 (44, bar 68 %), TARGET 5 (24), LAMBDA 6 (32, **danger**), THROTTLE 6 (32,
bar 92 %), IAT 4 (17), EGT 4 (17), KNOCK 4 (17). Status: `MAP 2` · `PEAK 1.61`.

**05 Timing** — LAP 7 (40), DELTA 5 (40), BEST 6 (22), LAST 6 (22), then the lap counter and timer
controls. Status: `LAP 4` · `BEST 1:36.08`.

**06 Controls** — launch rpm, clutch, launch, anti-lag, map, pit limit, cruise as engaged-button widgets
on the same 12-column grid.

## Motion — exhaustive, nothing else moves

- Values **snap**. Never tween a numeral.
- Bar gauges and the arc catch up in **120 ms linear**.
- Rev limit: hard on/off blink at **6 Hz**, no easing.
- Armed / engaged states: **1 s ease-in-out** pulse, 100 % → 35 % opacity.
- Stale signal (**500 ms** with no frame): value drops to Dim and renders `- -`, unit kept.
- Page change is an **instant cut** — no slide, no fade.
- Critical alert preempts every page: full `#FF4444` ground, pulsing, signal name in Archivo 800 tracked
  wide, value in huge mono, `STOP THE ENGINE` pinned at the bottom. Holds until acknowledged on the dash
  or from the phone.

## Overflow

The dash **never** auto-scales, wraps or clips a widget to make it fit. A layout that does not fit
320 × 240 is rejected at config load and the Tuner flags it before writing (`OUT OF BOUNDS` in the canvas
toolbar). Assert this on `PUT_CONFIG`.

## What to do

1. Render each of the six pages and diff against the Tuner canvas at 2×. Produce the deviation list
   first: page, widget, expected, actual.
2. Fix in this order: regenerate the two LVGL fonts at the device sizes in the table → theme tokens →
   widget grammar (kill any card/box/fill on value widgets, get the rules and paddings right) → grid
   spans per page → shift light and status row → buttons and arc → motion → the overflow assert.
3. Follow the firmware's own ownership rules (`2.3 LVGL ownership`, `2.4 Page lifecycle`): widgets are
   rebuilt on page change, never mutated across pages; no `lv_obj_clean` outside page teardown.
4. Do not add icons, radii, shadows, gradients, extra greys, a third red, or any easing not listed. If a
   memory or font-budget constraint forces a deviation, keep the grammar and document what changed and
   why.
