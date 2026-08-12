# CANShift dash — design system

The binding specification for the firmware UI (LVGL, CrowPanel 2.8″, 320 × 240, landscape).
Visual references: `CANShift Dash Pages.dc.html` in [CANShift/canshift-brand](https://github.com/CANShift/canshift-brand)
`design/` (the six pages rendered at 2×) and `DASH_PAGES.json` next to this file (the same six pages as
data). Where the firmware and this document disagree, the firmware is wrong.

This document replaces `PROMPT_DASH_IN_TUNER.md` and `PROMPT_DASH_POLISH.md`; the deviations still open
against it are tracked on issue #127.

Read the whole document before writing code. Then: audit, list deviations, fix in the order at the end.

---

## 0. The two rules everything else follows from

1. **A widget is a rule, a kicker and a value.** Not a card. There is no box, no fill, no border and no
   background behind a value anywhere on the dash. The horizontal rule above the widget is what separates
   it from its neighbour. The single exception is the button widget (§6), which is a 2 px outlined box.
2. **Nothing is centred, nothing is rounded, nothing eases.** Everything is flush left against its
   column. Radius is 0. The only motion is the six behaviours listed in §9.

If you are about to draw a rounded rectangle with a grey fill behind a number, stop — that is the drift
this document exists to correct.

## 1. Scale

The design references are authored at **2×**: a 640 × 480 box standing for the 320 × 240 panel.

- **Implement the device column.** Halve any measurement taken off a reference screenshot.
- Type is the exception: fonts are generated at the device sizes in §3, and no label goes below 10 px
  real. Where halving would give less than 10, use 10.

## 2. Colour

Two themes. Danger and engaged are the same in both — they are safety colours, not theme colours.

| Role | Token | NIGHT (default) | DAY |
| --- | --- | --- | --- |
| Ground | `CS_BG` | `#121212` | `#DDDDDD` |
| Ink — values, primary rules, button outlines | `CS_INK` | `#FFFFFF` | `#000000` |
| Dim — kickers, units, status row | `CS_DIM` | `#BABAB8` | `#5A5A5A` |
| Track — bar grounds, unlit shift cells, 1 px rules | `CS_TRACK` | `#222222` | `#C4C4C4` |
| Danger | `CS_DANGER` | `#FF4444` | `#FF4444` |
| Engaged | `CS_ENGAGED` | `#FF4747` | `#FF4747` |

Six colours. There is no seventh. No gradient, no shadow, no tint, no opacity below 100 % except the
75 % white used for a kicker on an engaged button and the pulse in §9.

**Danger vs engaged.** `#FF4444` means *this reading is out of range* — it colours the widget's rule,
kicker and value. `#FF4747` means *this function is on* — it fills a button. They are never swapped and
never mixed on the same widget.

## 3. Type

Two LVGL bitmap fonts, generated with `lv_font_conv`. Generate exactly the sizes used — no scaling at
runtime.

- **Archivo 800** — kickers and button state words only.
- **JetBrains Mono** — every value, every unit, the status row. **Tabular numerals are mandatory**: a
  value must not change width as its digits change.

| Class | Device | Canvas (2×) | Tracking | Line-height | Used for |
| --- | --- | --- | --- | --- | --- |
| `hero` | 48 | 96 | -0.045em | 0.92 | Street speed/gear, Timing lap/delta |
| `heroTrack` | 44 | 88 | -0.045em | 0.92 | Track rpm/gear, Tuning boost, Controls launch rpm |
| `primary` | 32 | 64 | -0.045em | 0.92 | the main readings of Engine, Tuning, Track |
| `mid` | 22 | 44 | -0.03em | 0.92 | Street rpm, Timing best/last/laps |
| `secondary` | 17 | 34 | -0.03em | 0.92 | every small reading |
| `button` | 14 | 28 | 0 | 1 | button state word |
| `kicker` | 10 | 10 | 0.18em | — | every widget label, UPPERCASE, in Dim |
| `unit` | 10 | 16 | — | — | in Dim, inline after the value |
| `statusRow` | 10 | 12 | 0.1em | — | the three status fields, in Dim |

Units are lowercase as the car states them (`km/h`, `rpm`, `bar`, `°C`, `%`, `V`, `km`, `λ`, `s`), set in
Dim, on the same baseline, one space after the value. Kickers are uppercase. No unit on a gear, a lap
time, a knock count or a button.

## 4. Frame and grid

| | Device | Canvas |
| --- | --- | --- |
| Panel | 320 × 240 | 640 × 480 |
| Outer padding, all four sides | 8 | 16 |
| Column count | 12 | 12 |
| Column gap | 6 | 12 |
| Row gap | 6 | 12 |
| Snap step used by the Tuner | 8 | 8 |

Content stacks: shift light (only where the page declares one) → status row → widget grid. The grid is
`align-content: start` — widgets keep their natural height and the page ends where the content ends. The
empty ground at the bottom of a short page is correct; do not stretch widgets to fill it.

Spans are out of 12 and are given per page in `DASH_PAGES.json`. Reproduce them exactly — they are what
the Tuner writes into the config.

## 5. Value widget

```
┌──────────────────────────  rule: 2px INK (primary) | 1px TRACK (secondary) | 2px DANGER
  KICKER                     Archivo 800, 10px, 0.18em, UPPERCASE, DIM
  184203 km                  Mono, class size, INK; unit 10px DIM
  ▓▓▓▓▓▓░░░░░░░░             optional bar, only when the signal is a percentage of a range
```

Device metrics:

| | Primary (`hero`/`heroTrack`/`primary`) | Secondary (`mid`/`secondary`) |
| --- | --- | --- |
| Rule | 2 px `CS_INK` | 1 px `CS_TRACK` |
| Padding top / bottom | 4 / 3 | 3 / 2 |
| Padding right | 8 | 8 |
| Padding left | **0** | **0** |
| Kicker → value gap | 2 | 1 |

- **Left padding is zero.** The kicker and the value sit flush on the column edge; only the right side is
  inset so two widgets do not touch.
- **Danger variant**: rule, kicker and value all in `CS_DANGER`; the unit stays Dim. Nothing else changes
  — no fill, no icon, no box, no blink.
- **Bar gauge** (only where `DASH_PAGES.json` gives a `bar`): 2 px tall device, ground `CS_TRACK`, fill
  `CS_INK` (or `CS_DANGER` in a danger widget), 4 px above it, right margin matching the widget, square
  ends, no border, no label.

## 6. Button widget

The only boxed widget. Used on Controls and for the Timing timer.

| | Device |
| --- | --- |
| Border | 2 px |
| Min height | 48 |
| Padding | 6 vertical / 7 horizontal |
| Content | kicker above, state word below, both flush left, vertically centred |

- **Idle**: border `CS_INK`, ground = the page ground, kicker Dim, state word Ink.
- **Engaged**: border **and** ground `CS_ENGAGED`, kicker white at 75 %, state word white.
- Real touch target never below 48 × 50 px device — widen the span rather than shrink the box.
- No unit, no icon, no state dot.

## 7. Shift light

Only on a page that declares it (Track). One row across the full content width:

- 12 equal cells, height 7 px device, gaps 2 px device, square, no border.
- Cells 1–7 `CS_INK`, cells 8–9 `CS_DANGER`, cells 10–12 `CS_TRACK` when unlit.
- Fills left to right with rpm. At the limiter the whole row blinks (§9).

## 8. Status row

One line, mono, Dim, 0.1em tracking, exactly three fields — left, centre, right:

| Page | Left | Centre | Right |
| --- | --- | --- | --- |
| Street | `CAN 842 Hz` | `MAP 1` | `TRIP 128 km` |
| Track | `CAN 842 Hz` | `MAP 1` | `LAP 4 — 1:38.42` |
| Engine | `CAN 842 Hz` | `MAP 1` | `MAX OIL 128` |
| Tuning | `CAN 842 Hz` | `MAP 2` | `PEAK 1.61` |
| Timing | `CAN 842 Hz` | `LAP 4` | `BEST 1:36.08` |
| Controls | `CAN 842 Hz` | `ALS ON` | `ARMED` |

Never a fourth field, never an icon, never a coloured status. The left field is always the bus rate.

## 9. Motion — the complete list

Nothing else on the dash moves.

| Behaviour | Rule |
| --- | --- |
| Value update | **snap**. Never tween a numeral. |
| Bar gauge / arc | catch up in 120 ms **linear** |
| Rev limit | hard on/off blink at 6 Hz, no easing |
| Engaged / armed state | 1 s ease-in-out pulse, 100 % → 35 % opacity |
| Stale signal (500 ms with no frame) | value drops to Dim and renders `- -`, unit kept |
| Page change | instant cut — no slide, no fade |

`- -` means **the sensor went quiet**. It is not the "no device" state, not a placeholder, and must never
appear in a preview or a demo.

## 10. The critical alert

Preempts every page. Full `CS_DANGER` ground, pulsing per §9; signal name in Archivo 800 tracked wide;
value in huge mono; `STOP THE ENGINE` pinned at the bottom. No status row, no page nav. Holds until
acknowledged on the dash or from the phone.

## 11. Overflow

The dash never auto-scales, wraps, ellipsises or clips a widget to make it fit. A layout that does not
fit 320 × 240 is **rejected at config load**, and the Tuner flags it before writing. Assert this on
`PUT_CONFIG` and fail loudly.

## 12. Checklist before you open a PR

- [ ] No fill, box, border or background behind any value widget.
- [ ] Left padding on every widget is 0; nothing is centred.
- [ ] Rules: 2 px ink on primaries, 1 px track on secondaries, 2 px danger on danger widgets.
- [ ] Kickers uppercase Archivo 800 at 10 px in Dim; values mono with tabular numerals.
- [ ] Units lowercase, in Dim, inline after the value; none on gear, lap, knock or a button.
- [ ] Spans match `DASH_PAGES.json` on all six pages.
- [ ] Status row has exactly three fields, bus rate on the left.
- [ ] Shift light: 7 ink + 2 danger + 3 track, 12 cells.
- [ ] Engaged buttons filled `#FF4747`; danger readings `#FF4444`; the two never swapped.
- [ ] No animation outside §9; page change is a cut.
- [ ] Radius 0, no shadow, no gradient, no seventh colour.

## 13. Order of work

1. Regenerate the two LVGL fonts at the §3 device sizes.
2. Theme tokens (§2), both themes.
3. The widget grammar (§5) — this is where the drift is; strip any card/box/fill first.
4. Grid and spans per page from `DASH_PAGES.json` (§4).
5. Shift light and status row (§7, §8).
6. Buttons (§6).
7. Motion (§9), then the alert (§10) and the overflow assert (§11).

Follow the firmware's own ownership rules (`2.3 LVGL ownership`, `2.4 Page lifecycle`): widgets are
rebuilt on page change, never mutated across pages; no `lv_obj_clean` outside page teardown. If a memory
or font-budget constraint forces a deviation, keep the grammar and document what changed and why.
