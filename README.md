# CANShift firmware bench

Latest: `33534f3` on 2026-10-16, 12 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,274,464 | `▁▁▁▁▁▁▁▁▇▇██` |
| crowpanel_28 | dram | 89,964 | `▁▁▁▁▁▁▁▁████` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,187,264 | `▁▁▁▁▁▁▁▁▇▇██` |
| generic_esp32s3 | dram | 83,704 | `▁▁▁▁▁▁▁▁████` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,274,656 | `▁▁▁▁▁▁▁▁▇▇██` |
| generic_ili9341 | dram | 89,964 | `▁▁▁▁▁▁▁▁████` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,274,944 | `▁▁▁▁▁▁▁▁▇▇██` |
| generic_ili9341_gt911 | dram | 89,964 | `▁▁▁▁▁▁▁▁████` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,634,784 | `▁▁▁▁▁▁▁▁▅▆██` |
| waveshare_s3_28 | dram | 105,164 | `▁▁▁▁▁▁▁▁████` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 72.7 | `▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 176.2 | `▁████▅▄█▄█▄█` |
| expr_eval | 47.3 | `▁█▇██▆▆█▆█▆▇` |
| telemetry_encode | 66.1 | `▁▇▆▇▇██▇▆▇▆▇` |
| status_writer | 1,461.5 | `▁█▇▇█▄▄█▆█▇▇` |
| format_float | 23.4 | `▁████▇▇█▄█▄█` |
| command/status | 1,775.0 | `▁████▄▄█▆█▆▇` |
| command/ping | 326.4 | `▂▇▆▆▆▅▆▆▂█▁█` |
| command/shared | 255.1 | `▂▆█▆▆▅▆▆▁▆▁▅` |
| command/config_get | 9,234.8 | `▇▇▇▇▇▁▁▇█▇█▇` |
| command/ota_reject | 999.8 | `▁████▇▇█▄█▃▇` |
| command/unknown | 285.0 | `▁█▇▇▇▅▅▇▂▇▁▆` |
| command/parse_error | 265.6 | `▂███▇▆▆█▁█▁▇` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
| `c533430` | 2026-10-15 | 6/6 | 30/30 |
| `d1e2f98` | 2026-10-15 | 6/6 | 30/30 |
