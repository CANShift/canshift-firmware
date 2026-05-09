# Icon Sources

All sensor and UI icon SVGs in this directory are derived from the Phosphor
Icons (Fill weight) set, pulled from
https://github.com/phosphor-icons/core at commit
`2b75f3ad12b420c9504ef05df8d2564a28f8500e` (version 2.x family, fetched
2026-05-09). Each upstream SVG is patched with `fill="#FFFFFF"` on the root
`<svg>` element so `rsvg-convert` rasterises the glyph in white on a
transparent background — the format the LVGL pipeline expects.

Phosphor Icons are MIT-licensed; the full license text lives next to this
file in `PHOSPHOR_LICENSE`. The bundled `.bin` artefacts under
`canshift-firmware/data/assets/` are derived works covered by the same MIT
notice.

## Mapping (target stem -> upstream Phosphor name)

### Sensors (`sensors/*.svg`, 32x32 cf=5 outputs)

| Target              | Phosphor source           | Notes                                |
| ------------------- | ------------------------- | ------------------------------------ |
| rpm                 | gauge-fill                |                                      |
| speed               | speedometer-fill          |                                      |
| coolant             | thermometer-cold-fill     |                                      |
| oil_pressure        | drop-fill                 | Soft mapping: no oil-can-fill exists |
| oil_temp            | thermometer-hot-fill      |                                      |
| battery             | car-battery-fill          |                                      |
| fuel                | gas-pump-fill             |                                      |
| afr                 | flame-fill                | Intentional dup with sensor_flame    |
| boost               | wind-fill                 |                                      |
| throttle            | arrow-fat-up-fill         |                                      |
| iat                 | thermometer-simple-fill   |                                      |
| gear                | gear-six-fill             |                                      |
| timer               | timer-fill                |                                      |
| warning             | warning-octagon-fill      |                                      |
| flame               | flame-fill                | Intentional dup with sensor_afr      |
| turbo               | fan-fill                  |                                      |
| engine              | engine-fill               |                                      |
| brake               | circle-half-tilt-fill     |                                      |
| launch              | rocket-launch-fill        |                                      |
| traction            | tire-fill                 | Soft mapping: best-fit compromise    |
| map_icon            | map-trifold-fill          |                                      |
| exhaust             | cloud-fog-fill            | Soft mapping: no smoke-fill exists   |
| cog                 | gear-fill                 |                                      |

### UI (`ui/*.svg`, 12x12 cf=5 outputs)

The target stem maps directly to the firmware filename
(`icon_<stem>.bin`); the firmware loader still references
`icon_day.bin` / `icon_night.bin`, so the source stems are kept as
`day` / `night` even though the upstream Phosphor names are sun-fill
and moon-fill.

| Target | Phosphor source |
| ------ | --------------- |
| day    | sun-fill        |
| night  | moon-fill       |
