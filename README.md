# CANShift firmware bench

Latest: `083ca2e` on 2026-10-16, 19 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,279,664 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅█` |
| crowpanel_28 | dram | 90,300 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅▅▅▅█` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,192,592 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅█` |
| generic_esp32s3 | dram | 84,056 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅▅▅▅█` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,279,840 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅█` |
| generic_ili9341 | dram | 90,300 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅▅▅▅█` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,280,144 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅█` |
| generic_ili9341_gt911 | dram | 90,300 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅▅▅▅█` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,641,344 | `▁▁▁▁▁▁▁▁▂▂▃▃▃▃▄▅▅▆█` |
| waveshare_s3_28 | dram | 105,524 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▄▅▅▅█` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 72.7 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 178.3 | `▅█▇█▇▆▆█▆█▆█▇▆▆█▁▅█` |
| expr_eval | 47.3 | `▁█▇██▆▆█▆█▆▇▇▄▄▇▄▁▇` |
| telemetry_encode | 66.7 | `▁▇▆▇▇██▇▆▇▆▇▇▆▆▇▅▁▇` |
| status_writer | 1,457.2 | `▁█▇▇█▄▄█▆█▇▇▇▇▇▇▃▁▇` |
| format_float | 23.2 | `▁▆▆▆▆▅▆▆▃▆▃▆▆▃▃▆█▁▆` |
| command/status | 1,825.2 | `▁████▄▄█▆█▆▇▇▆▆▇▄▁█` |
| command/ping | 308.8 | `▂▇▆▆▆▅▆▆▂█▁██▂▂▇▆▂▇` |
| command/shared | 258.4 | `▂▆█▆▆▅▆▆▁▆▁▅▆▁▁▅▆▄▅` |
| command/config_get | 9,200.4 | `▇▇▇▇▇▁▁▇█▇█▇▇▇█▇▁▇▇` |
| command/ota_reject | 988.2 | `▁████▇▇█▄█▃▇█▄▄▇▇▁▇` |
| command/unknown | 269.7 | `▁█▇▇▇▅▅▇▂▇▁▆▇▂▂█▆▁▅` |
| command/parse_error | 244.8 | `▂▇▇▇▇▅▆▇▁▇▁▇▇▁▂█▇▂▅` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `083ca2e` | 2026-10-16 | 6/6 | 30/30 |
| `a37495b` | 2026-10-16 | 6/6 | 30/30 |
| `afd59e6` | 2026-10-16 | 6/6 | 30/30 |
| `72b873f` | 2026-10-16 | 6/6 | 30/30 |
| `b21d6e9` | 2026-10-16 | 6/6 | 30/30 |
| `168b33d` | 2026-10-16 | 6/6 | 30/30 |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
