# CANShift firmware bench

Latest: `b21d6e9` on 2026-10-16, 15 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,275,168 | `▁▁▁▁▁▁▁▁▅▆▆▆▆▇█` |
| crowpanel_28 | dram | 89,988 | `▁▁▁▁▁▁▁▁███████` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,188,016 | `▁▁▁▁▁▁▁▁▅▆▆▆▆▇█` |
| generic_esp32s3 | dram | 83,728 | `▁▁▁▁▁▁▁▁███▇▇▇█` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,275,344 | `▁▁▁▁▁▁▁▁▅▆▆▆▆▇█` |
| generic_ili9341 | dram | 89,988 | `▁▁▁▁▁▁▁▁███████` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,275,648 | `▁▁▁▁▁▁▁▁▅▆▆▆▆▇█` |
| generic_ili9341_gt911 | dram | 89,988 | `▁▁▁▁▁▁▁▁███████` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,635,824 | `▁▁▁▁▁▁▁▁▄▄▆▆▇▇█` |
| waveshare_s3_28 | dram | 105,180 | `▁▁▁▁▁▁▁▁███████` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 63.4 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 149.0 | `▁████▅▄█▄█▄██▅▅` |
| expr_eval | 32.5 | `▁█▇██▆▆█▆█▆▇▇▄▄` |
| telemetry_encode | 56.6 | `▁▇▆▇▇██▇▆▇▆▇▇▆▆` |
| status_writer | 1,248.7 | `▁█▇▇█▄▄█▆█▇▇▇▇▇` |
| format_float | 17.2 | `▁████▇▇█▄█▄██▄▄` |
| command/status | 1,475.9 | `▁████▄▄█▆█▆▇▇▆▆` |
| command/ping | 205.4 | `▂▇▆▆▆▅▆▆▂█▁██▂▂` |
| command/shared | 178.4 | `▂▆█▆▆▅▆▆▁▆▁▅▆▁▁` |
| command/config_get | 8,694.8 | `▇▇▇▇▇▁▁▇█▇█▇▇▇█` |
| command/ota_reject | 716.3 | `▁████▇▇█▄█▃▇█▄▄` |
| command/unknown | 188.5 | `▁█▇▇▇▅▅▇▂▇▁▆▇▂▂` |
| command/parse_error | 173.8 | `▂███▇▆▆█▁█▁▇▇▁▂` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `b21d6e9` | 2026-10-16 | 6/6 | 30/30 |
| `168b33d` | 2026-10-16 | 6/6 | 30/30 |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
