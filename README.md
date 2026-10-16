# CANShift firmware bench

Latest: `ff4acd8` on 2026-10-16, 13 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,274,464 | `▁▁▁▁▁▁▁▁▇▇███` |
| crowpanel_28 | dram | 89,964 | `▁▁▁▁▁▁▁▁█████` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,187,264 | `▁▁▁▁▁▁▁▁▇▇███` |
| generic_esp32s3 | dram | 83,704 | `▁▁▁▁▁▁▁▁█████` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,274,656 | `▁▁▁▁▁▁▁▁▇▇███` |
| generic_ili9341 | dram | 89,964 | `▁▁▁▁▁▁▁▁█████` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,274,944 | `▁▁▁▁▁▁▁▁▇▇███` |
| generic_ili9341_gt911 | dram | 89,964 | `▁▁▁▁▁▁▁▁█████` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,635,056 | `▁▁▁▁▁▁▁▁▅▅▇▇█` |
| waveshare_s3_28 | dram | 105,172 | `▁▁▁▁▁▁▁▁█████` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 72.9 | `▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 176.1 | `▁████▅▄█▄█▄██` |
| expr_eval | 47.0 | `▁█▇██▆▆█▆█▆▇▇` |
| telemetry_encode | 66.2 | `▁▇▆▇▇██▇▆▇▆▇▇` |
| status_writer | 1,474.9 | `▁█▇▇█▄▄█▆█▇▇▇` |
| format_float | 23.3 | `▁████▇▇█▄█▄██` |
| command/status | 1,778.5 | `▁████▄▄█▆█▆▇▇` |
| command/ping | 325.7 | `▂▇▆▆▆▅▆▆▂█▁██` |
| command/shared | 261.9 | `▂▆█▆▆▅▆▆▁▆▁▅▆` |
| command/config_get | 9,243.7 | `▇▇▇▇▇▁▁▇█▇█▇▇` |
| command/ota_reject | 1,008.7 | `▁████▇▇█▄█▃▇█` |
| command/unknown | 289.5 | `▁█▇▇▇▅▅▇▂▇▁▆▇` |
| command/parse_error | 266.7 | `▂███▇▆▆█▁█▁▇▇` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
| `c533430` | 2026-10-15 | 6/6 | 30/30 |
