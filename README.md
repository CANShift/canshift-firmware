# CANShift firmware bench

Latest: `2ef5522` on 2026-10-15, 5 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,272,496 | `▅▅▅▅▅` |
| crowpanel_28 | dram | 89,644 | `▅▅▅▅▅` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,185,248 | `▅▅▅▅▅` |
| generic_esp32s3 | dram | 83,400 | `▅▅▅▅▅` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅` |
| generic_ili9341 | flash | 1,272,672 | `▅▅▅▅▅` |
| generic_ili9341 | dram | 89,644 | `▅▅▅▅▅` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,272,960 | `▅▅▅▅▅` |
| generic_ili9341_gt911 | dram | 89,644 | `▅▅▅▅▅` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,632,112 | `▅▅▅▅▅` |
| waveshare_s3_28 | dram | 104,844 | `▅▅▅▅▅` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 72.8 | `▅▅▅▅▅` |
| can_decode | 176.2 | `▁████` |
| expr_eval | 48.6 | `▁█▇██` |
| telemetry_encode | 66.2 | `▁████` |
| status_writer | 1,531.0 | `▁█▇▇█` |
| format_float | 23.1 | `▁████` |
| command/status | 1,853.3 | `▁████` |
| command/ping | 302.1 | `▁█▇▇▇` |
| command/shared | 265.8 | `▁▆█▆▆` |
| command/config_get | 9,081.4 | `▅█▅▇▁` |
| command/ota_reject | 1,022.2 | `▁████` |
| command/unknown | 290.0 | `▁█▇▇▇` |
| command/parse_error | 265.8 | `▁███▇` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
| `c533430` | 2026-10-15 | 6/6 | 30/30 |
| `d1e2f98` | 2026-10-15 | 6/6 | 30/30 |
| `02371df` | 2026-10-15 | 6/6 | 30/30 |
| `47d88a7` | 2026-10-15 | 6/6 | 30/30 |
