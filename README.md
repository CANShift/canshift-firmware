# CANShift firmware bench

Latest: `72b873f` on 2026-10-16, 16 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,276,704 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅█` |
| crowpanel_28 | dram | 89,988 | `▁▁▁▁▁▁▁▁████████` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,189,552 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▆█` |
| generic_esp32s3 | dram | 83,744 | `▁▁▁▁▁▁▁▁███▇▇▇██` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,276,880 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅█` |
| generic_ili9341 | dram | 89,988 | `▁▁▁▁▁▁▁▁████████` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,277,184 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▅▅█` |
| generic_ili9341_gt911 | dram | 89,988 | `▁▁▁▁▁▁▁▁████████` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,637,456 | `▁▁▁▁▁▁▁▁▃▃▄▅▅▅▆█` |
| waveshare_s3_28 | dram | 105,196 | `▁▁▁▁▁▁▁▁▇▇▇▇████` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 72.8 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 178.2 | `▁▇▇▇▇▅▄▇▃▇▃▇▇▅▅█` |
| expr_eval | 47.4 | `▁█▇██▆▆█▆█▆▇▇▄▄▇` |
| telemetry_encode | 66.1 | `▁▇▆▇▇██▇▆▇▆▇▇▆▆▇` |
| status_writer | 1,467.2 | `▁█▇▇█▄▄█▆█▇▇▇▇▇▇` |
| format_float | 23.1 | `▁████▇▇█▄█▄██▄▄█` |
| command/status | 1,772.8 | `▁████▄▄█▆█▆▇▇▆▆▇` |
| command/ping | 317.4 | `▂▇▆▆▆▅▆▆▂█▁██▂▂▇` |
| command/shared | 257.0 | `▂▆█▆▆▅▆▆▁▆▁▅▆▁▁▅` |
| command/config_get | 9,237.3 | `▇▇▇▇▇▁▁▇█▇█▇▇▇█▇` |
| command/ota_reject | 982.7 | `▁████▇▇█▄█▃▇█▄▄▇` |
| command/unknown | 303.8 | `▁█▇▇▇▅▅▇▂▇▁▆▇▂▂█` |
| command/parse_error | 281.0 | `▂▇▇▇▇▅▆▇▁▇▁▇▇▁▂█` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `72b873f` | 2026-10-16 | 6/6 | 30/30 |
| `b21d6e9` | 2026-10-16 | 6/6 | 30/30 |
| `168b33d` | 2026-10-16 | 6/6 | 30/30 |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
