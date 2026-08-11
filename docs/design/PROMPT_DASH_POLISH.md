# Prompt — CANShift dash: the remaining polish pass

Companion to `~/Downloads/design_handoff_mobile_dash/PROMPT_DASH_DESIGN.md` (still the binding
token/type/grammar spec) and the six page mockups in `CANShift Brand Book.dc.html` §04. This
prompt is self-sufficient: it records exactly what already conforms, every remaining deviation,
and the order to fix them. **Verify every change in the simulator before pushing**
(`pio run -e sim && .pio/build/sim/program` — see `sim/README.md` for the injector keys).

## Already conform — do not redo

- Tokens: ground `#121212` edge to edge, ink/muted/track/accent/danger per the table; no greens,
  no third red, radius 0 everywhere.
- Type: JB Mono 72/46/34 + Medium 14 units, Archivo 800 labels 10–28; tabular; primary tracking
  −3 px; kickers 10 px at 2 px tracking, muted.
- Widget grammar: top rule per tier (2 px ink / 1 px track / 2 px danger), kicker above value
  flush left, values render whole at tier size, units 14 muted inline, 3 px bars (track/ink,
  red in danger), danger tier (rule+kicker+value red, secondaries swap to 46).
- Buttons: outlined 2 px ink / filled `#FF4747` engaged, kicker (config `kicker` > signal label >
  action-derived PAGE/MAP/CRUISE) + UPPERCASE state word ~28 px, flush left, length-aware ladder.
- Stale: grey `- -` at 500 ms. Critical alert takeover: full `#FF4444` 1 s pulse, name tracked
  wide, mono 72 value, STOP THE ENGINE, ack by tap or physical button, OTA precedence.
- Default pages rebuilt to the mockup compositions (schema 1.32.0).

## Remaining deviations — fix in this order

### 1. Missing signals the mockups assume (catalog + injector work, no rendering change)

`boost_bar`, `afr_1`, `fuel_level_pct`, `egt_c`, `gearbox_temp_c`, `diff_temp_c`, `knock_count`,
`clutch_state`, `odo_km`, `trip_km`. The Tuning page currently substitutes MAP for BOOST and
lambda for AFR; Street substitutes MAP for ODO. Add the signals to the catalog (+ CAN frames in
`signal_map`), extend the sim injector, then point the default pages at the real signals.

### 2. Per-page top-bar right slot (schema + firmware)

Mockups: TRIP 128 km (Street), LAP 4 — 1:38.42 (Track), MAX OIL 128 (Engine), PEAK 1.61
(Tuning), BEST 1:36.08 (Timing), ARMED (Controls). Today the top bar is global. Needs:
per-page override of the right slot in the schema, plus min/max/peak aggregates tracked
firmware-side (a small `SignalStats` service) and lap/best from `timer_service`/`track_store`.

### 3. Low-side danger thresholds

The mockups show OIL PRESS in the danger tier at 1.1 bar — a LOW-value danger. Widget-level
danger (`dangerLevel`, `alertThreshold`) is high-side only; the alert engine already knows
low-side. Add `dangerBelow` (or `invertDanger`) to gauge/label configs, mirror in firmware and
the tuner preview, then set it on the oil-pressure widgets of Track/Engine.

### 4. Timing page (needs #2 + lap data)

Mockup: LAP time + DELTA (−0.42 s, mono huge), BEST/LAST/LAPS row, TIMER→RUNNING as an engaged
(filled red) button, RESET LAP button. Delta/best/last need lap capture plumbed to widgets
(`track_store` exists); the timer state button is a new widget variant (button grammar bound to
timer state, engaged while running).

### 5. Shift light above the top bar (Track)

Mockup stacks: shift strip, then the CAN/MAP/LAP row. Widgets render below the top bar, so the
strip currently sits under it. Either allow the top bar to yield row 0 to a full-width
shift-light widget, or add a per-page `topBarBelowShiftLight` arrangement. Small, but decide the
mechanism — do not hack the widget's y offset.

### 6. System screens (existing issues)

- **#7 OTA**: kicker `UPDATING FIRMWARE` (0.2 em), mono percent 56 px (font to generate — fits,
  ~190 KB headroom) with ` %` 20 px muted, 10 px bar pinned bottom (`#222222`/`#FF4747`),
  `DO NOT UNPLUG` 12 px `#FF8800` under it; explicit COMPLETE and FAILED screens (FAILED on
  `#FF4444` per the brand book); kill the spinner arc, breathing USB icon and rounded radii.
- **Boot**: monogram + wordmark on `#121212`, no progress bar — verify against
  `gen_monogram_c.py` output in the sim.

### 7. Motion (#9)

Verify in the sim: rev-limit 6 Hz hard blink (exists), armed 1 s pulse 100→35 % amplitude,
bars catch up 120 ms linear (bars currently snap), values snap (now true). Then settle the
open question: widget-level `AlertFlash` blink is NOT in the spec's exhaustive motion list —
either remove it (danger tier + takeover already carry the alarm) or get it added to the spec.

### 8. Small fry

- `FRAME_PADDING` 16 → 22 (`layout-grid`, core + firmware + tuner canvas).
- Warning-widget icon: the last icon on a dash page — decide (spec says no icons).
- Cruise page rework to the button grammar (with the #4 hover artifact).
- Top-bar `CAN 842 Hz` format: mockups show the real rate, the sim stub shows raw Hz — check
  the device formatting, and feed `map_number` in the sim injector so MAP stops showing `- -`.

## Acceptance

Every item verified in the simulator against the corresponding mockup page screenshot before
its PR; deviations that survive (hardware limits, missing data) documented in the PR body per
the spec's escape hatch.
