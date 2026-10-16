# CANShift firmware bench

Latest: `7d19727` on 2026-10-16, 20 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,279,696 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅██` |
| crowpanel_28 | dram | 90,316 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅▅▅▅██` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,192,624 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅██` |
| generic_esp32s3 | dram | 84,072 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▄▅▅▅██` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,279,872 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅██` |
| generic_ili9341 | dram | 90,316 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅▅▅▅██` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,280,176 | `▁▁▁▁▁▁▁▁▃▃▃▃▃▃▄▅▅▅██` |
| generic_ili9341_gt911 | dram | 90,316 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▅▅▅▅██` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,641,872 | `▁▁▁▁▁▁▁▁▂▂▃▃▃▃▄▅▅▆██` |
| waveshare_s3_28 | dram | 105,524 | `▁▁▁▁▁▁▁▁▄▄▄▄▄▄▄▅▅▅██` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 56.3 | `▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 126.5 | `▅█▇█▇▆▆█▆█▆█▇▆▆█▁▅█▅` |
| expr_eval | 22.7 | `▁█▇██▆▆█▆█▆▇▇▄▄▇▄▁▇▁` |
| telemetry_encode | 34.6 | `▁▇▆▇▇██▇▆▇▆▇▇▆▆▇▅▁▇▁` |
| status_writer | 721.1 | `▁█▇▇█▄▄█▆█▇▇▇▇▇▇▃▂▇▁` |
| format_float | 12.8 | `▁▆▆▆▆▅▆▆▃▆▃▆▆▃▃▆█▁▆▁` |
| command/status | 908.1 | `▁████▄▅█▆█▆▇▇▆▆▇▄▁█▁` |
| command/ping | 181.2 | `▂▇▆▆▆▅▆▆▂█▁██▂▂▇▆▂▇▂` |
| command/shared | 165.4 | `▂▆█▆▆▅▆▆▁▆▁▅▆▁▁▅▆▄▅▂` |
| command/config_get | 7,048.1 | `▇▇▇▇▇▁▁▇█▇█▇▇▇█▇▁▇▇▇` |
| command/ota_reject | 532.3 | `▁████▇▇█▄█▃▇█▄▄▇▇▁▇▁` |
| command/unknown | 157.8 | `▁█▇▇▇▅▅▇▂▇▁▆▇▂▂█▆▁▅▁` |
| command/parse_error | 154.9 | `▂▇▇▇▇▅▆▇▁▇▁▇▇▁▂█▇▂▅▂` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `7d19727` | 2026-10-16 | 6/6 | 30/30 |
| `083ca2e` | 2026-10-16 | 6/6 | 30/30 |
| `a37495b` | 2026-10-16 | 6/6 | 30/30 |
| `afd59e6` | 2026-10-16 | 6/6 | 30/30 |
| `72b873f` | 2026-10-16 | 6/6 | 30/30 |
| `b21d6e9` | 2026-10-16 | 6/6 | 30/30 |
| `168b33d` | 2026-10-16 | 6/6 | 30/30 |
| `ff4acd8` | 2026-10-16 | 6/6 | 30/30 |
| `33534f3` | 2026-10-16 | 6/6 | 30/30 |
| `888a17e` | 2026-10-16 | 6/6 | 30/30 |
