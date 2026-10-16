# CANShift firmware bench

Latest: `a37495b` on 2026-10-16, 18 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,276,960 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅███` |
| crowpanel_28 | dram | 89,988 | `▁▁▁▁▁▁▁▁██████████` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,189,824 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅███` |
| generic_esp32s3 | dram | 83,744 | `▁▁▁▁▁▁▁▁███▇▇▇████` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,277,152 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅███` |
| generic_ili9341 | dram | 89,988 | `▁▁▁▁▁▁▁▁██████████` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,277,440 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅███` |
| generic_ili9341_gt911 | dram | 89,988 | `▁▁▁▁▁▁▁▁██████████` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,638,576 | `▁▁▁▁▁▁▁▁▃▃▄▄▄▅▅▇▇█` |
| waveshare_s3_28 | dram | 105,196 | `▁▁▁▁▁▁▁▁▇▇▇▇██████` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 60.1 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 135.6 | `▅████▆▆█▆█▆██▇▇█▁▅` |
| expr_eval | 25.0 | `▁█▇██▆▆█▆█▆▇▇▄▄▇▄▁` |
| telemetry_encode | 37.2 | `▁▇▆▇▇██▇▆▇▆▇▇▆▆▇▅▁` |
| status_writer | 807.1 | `▁█▇▇█▄▄█▆█▇▇▇▇▇▇▃▁` |
| format_float | 13.9 | `▁▆▆▆▆▅▆▆▃▆▃▆▆▃▃▆█▁` |
| command/status | 992.1 | `▁████▄▄█▆█▆▇▇▆▆▇▄▁` |
| command/ping | 195.2 | `▂▇▆▆▆▅▆▆▂█▁██▂▂▇▆▂` |
| command/shared | 194.1 | `▂▆█▆▆▅▆▆▁▆▁▅▆▁▁▅▆▄` |
| command/config_get | 7,619.1 | `▇▇▇▇▇▁▁▇█▇█▇▇▇█▇▁▇` |
| command/ota_reject | 565.6 | `▁████▇▇█▄█▃▇█▄▄▇▇▁` |
| command/unknown | 173.9 | `▁█▇▇▇▅▅▇▂▇▁▆▇▂▂█▆▁` |
| command/parse_error | 165.5 | `▂▇▇▇▇▅▆▇▁▇▁▇▇▁▂█▇▂` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `a37495b` | 2026-10-16 | 6/6 | 30/30 |
| `afd59e6` | 2026-10-16 | 6/6 | 30/30 |
| `72b873f` | 2026-10-16 | 6/6 | 30/30 |
| `b21d6e9` | 2026-10-16 | 6/6 | 30/30 |
| `168b33d` | 2026-10-16 | 6/6 | 30/30 |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
