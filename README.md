# CANShift firmware bench

Latest: `168b33d` on 2026-10-16, 14 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,274,704 | `▁▁▁▁▁▁▁▁▆▇▇▇▇█` |
| crowpanel_28 | dram | 89,964 | `▁▁▁▁▁▁▁▁██████` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,187,552 | `▁▁▁▁▁▁▁▁▆▆▇▇▇█` |
| generic_esp32s3 | dram | 83,704 | `▁▁▁▁▁▁▁▁██████` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,274,896 | `▁▁▁▁▁▁▁▁▆▇▇▇▇█` |
| generic_ili9341 | dram | 89,964 | `▁▁▁▁▁▁▁▁██████` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,275,184 | `▁▁▁▁▁▁▁▁▆▇▇▇▇█` |
| generic_ili9341_gt911 | dram | 89,964 | `▁▁▁▁▁▁▁▁██████` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,635,344 | `▁▁▁▁▁▁▁▁▅▅▇▇▇█` |
| waveshare_s3_28 | dram | 105,172 | `▁▁▁▁▁▁▁▁██████` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 89.7 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 210.9 | `▁████▅▄█▄█▄██▅` |
| expr_eval | 46.7 | `▁█▇██▆▆█▆█▆▇▇▄` |
| telemetry_encode | 80.1 | `▁▇▆▇▇██▇▆▇▆▇▇▆` |
| status_writer | 1,738.3 | `▁█▇▇█▄▄█▆█▇▇▇▇` |
| format_float | 24.0 | `▁████▇▇█▄█▄██▄` |
| command/status | 2,086.9 | `▁████▄▄█▆█▆▇▇▆` |
| command/ping | 289.0 | `▂▇▆▆▆▅▆▆▂█▁██▂` |
| command/shared | 251.8 | `▂▆█▆▆▅▆▆▁▆▁▅▆▁` |
| command/config_get | 11,627.7 | `▇▇▇▇▇▁▁▇█▇█▇▇▇` |
| command/ota_reject | 1,020.6 | `▁████▇▇█▄█▃▇█▄` |
| command/unknown | 267.4 | `▁█▇▇▇▅▅▇▂▇▁▆▇▂` |
| command/parse_error | 242.2 | `▂███▇▆▆█▁█▁▇▇▁` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `168b33d` | 2026-10-16 | 6/6 | 30/30 |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
