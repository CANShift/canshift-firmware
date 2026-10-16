# CANShift firmware bench

Latest: `888a17e` on 2026-10-16, 11 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,274,336 | `▁▁▁▁▁▁▁▁▇██` |
| crowpanel_28 | dram | 89,964 | `▁▁▁▁▁▁▁▁███` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,187,152 | `▁▁▁▁▁▁▁▁▇██` |
| generic_esp32s3 | dram | 83,720 | `▁▁▁▁▁▁▁▁███` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,274,528 | `▁▁▁▁▁▁▁▁▇██` |
| generic_ili9341 | dram | 89,964 | `▁▁▁▁▁▁▁▁███` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,274,816 | `▁▁▁▁▁▁▁▁▇██` |
| generic_ili9341_gt911 | dram | 89,964 | `▁▁▁▁▁▁▁▁███` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,634,656 | `▁▁▁▁▁▁▁▁▆▆█` |
| waveshare_s3_28 | dram | 105,164 | `▁▁▁▁▁▁▁▁███` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 63.4 | `▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 146.0 | `▁████▅▄█▄█▄` |
| expr_eval | 36.9 | `▁█▇██▆▆█▆█▆` |
| telemetry_encode | 56.4 | `▁▇▆▇▇██▇▆▇▆` |
| status_writer | 1,230.5 | `▁█▇▇█▄▄█▆█▇` |
| format_float | 17.2 | `▁████▇██▄█▄` |
| command/status | 1,426.3 | `▁████▄▄█▆█▆` |
| command/ping | 196.3 | `▂▇▆▆▆▅▆▆▂█▁` |
| command/shared | 181.5 | `▂▆█▆▆▅▆▆▁▆▁` |
| command/config_get | 8,792.4 | `▇▇▇▇▇▁▁▇█▇█` |
| command/ota_reject | 700.5 | `▁████▇▇█▄█▃` |
| command/unknown | 177.3 | `▁█▇▇▇▅▅▇▂▇▁` |
| command/parse_error | 167.7 | `▂███▇▆▆█▁█▁` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
| `c533430` | 2026-10-15 | 6/6 | 30/30 |
| `d1e2f98` | 2026-10-15 | 6/6 | 30/30 |
| `02371df` | 2026-10-15 | 6/6 | 30/30 |
