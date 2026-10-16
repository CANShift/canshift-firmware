# CANShift firmware bench

Latest: `afd59e6` on 2026-10-16, 17 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,276,704 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅██` |
| crowpanel_28 | dram | 89,988 | `▁▁▁▁▁▁▁▁█████████` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,189,568 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅██` |
| generic_esp32s3 | dram | 83,744 | `▁▁▁▁▁▁▁▁███▇▇▇███` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,276,880 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅██` |
| generic_ili9341 | dram | 89,988 | `▁▁▁▁▁▁▁▁█████████` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,277,184 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅██` |
| generic_ili9341_gt911 | dram | 89,988 | `▁▁▁▁▁▁▁▁█████████` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,637,472 | `▁▁▁▁▁▁▁▁▃▃▄▄▅▅▆██` |
| waveshare_s3_28 | dram | 105,196 | `▁▁▁▁▁▁▁▁▇▇▇▇█████` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 71.7 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 142.2 | `▅████▆▆█▆█▆██▇▇█▁` |
| expr_eval | 38.6 | `▁█▇██▆▆█▆█▆▇▇▄▄▇▄` |
| telemetry_encode | 59.8 | `▁▇▆▇▇██▇▆▇▆▇▇▆▆▇▅` |
| status_writer | 1,095.0 | `▁█▇▇█▄▄█▆█▇▇▇▇▇▇▃` |
| format_float | 25.7 | `▁▆▆▆▆▅▆▆▃▆▃▆▆▃▃▆█` |
| command/status | 1,411.4 | `▁████▄▄█▆█▆▇▇▆▆▇▄` |
| command/ping | 296.6 | `▂▇▆▆▆▅▆▆▂█▁██▂▂▇▆` |
| command/shared | 263.5 | `▂▆█▆▆▅▆▆▁▆▁▅▆▁▁▅▆` |
| command/config_get | 4,689.0 | `▇▇▇▇▇▁▁▇█▇█▇▇▇█▇▁` |
| command/ota_reject | 986.9 | `▁████▇▇█▄█▃▇█▄▄▇▇` |
| command/unknown | 278.1 | `▁█▇▇▇▅▅▇▂▇▁▆▇▂▂█▆` |
| command/parse_error | 264.0 | `▂▇▇▇▇▅▆▇▁▇▁▇▇▁▂█▇` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `afd59e6` | 2026-10-16 | 6/6 | 30/30 |
| `72b873f` | 2026-10-16 | 6/6 | 30/30 |
| `b21d6e9` | 2026-10-16 | 6/6 | 30/30 |
| `168b33d` | 2026-10-16 | 6/6 | 30/30 |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
