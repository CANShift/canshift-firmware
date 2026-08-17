# Cruise control

The dash ships with a dedicated cruise control page — four L-shape buttons
around a centred set-speed readout. The layout mirrors what
`canshift-tuner/src/components/editor/CruiseControlPreview.tsx` renders, and
the firmware tracks the same geometry pixel-by-pixel.

## Layout

The page is a 320 × 240 grid of four L-shape buttons around a centred
set-speed readout. Top-left **−** and top-right **+** form the upper row;
bottom-left **SET** and bottom-right **OFF** form the lower row.

- **+ / −** adjust the set-speed (typically 1 km/h per tap — depends on tuner
  config).
- **SET** saves the current speed as the set-speed and arms cruise.
- **OFF** disarms cruise — the "ON" badge disappears.

## The "ON" indicator

When cruise is armed, the OFF button shows a small "ON" overlay badge. Tap to
disarm.

## No reading in the centre?

If the centre shows `0` instead of the set-speed:

- Check that your ECU emits the cruise signal (typically the frame ID
  configured in `signals.json` for `cruise_setpoint`).
- The widget shows `0` when the signal is invalid (no frame received in the
  last ~1 s). See [SignalStore](../architecture/signal-store.md).

## Technical details

The cruise page renderer is non-trivial (L shapes, LVGL convex-polygon
constraint). See [Cruise template](../architecture/cruise-template.md).
