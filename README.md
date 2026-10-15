# CANShift firmware bench

Latest: `59df1cf` on 2026-10-15, 6 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,272,496 | `▅▅▅▅▅▅` |
| crowpanel_28 | dram | 89,644 | `▅▅▅▅▅▅` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,185,248 | `▅▅▅▅▅▅` |
| generic_esp32s3 | dram | 83,400 | `▅▅▅▅▅▅` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,272,672 | `▅▅▅▅▅▅` |
| generic_ili9341 | dram | 89,644 | `▅▅▅▅▅▅` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,272,960 | `▅▅▅▅▅▅` |
| generic_ili9341_gt911 | dram | 89,644 | `▅▅▅▅▅▅` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,632,112 | `▅▅▅▅▅▅` |
| waveshare_s3_28 | dram | 104,844 | `▅▅▅▅▅▅` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 65.0 | `▅▅▅▅▅▅` |
| can_decode | 152.3 | `▁████▅` |
| expr_eval | 39.5 | `▁█▇██▆` |
| telemetry_encode | 63.1 | `▁▇▇▇▇█` |
| status_writer | 1,060.6 | `▁█▇▇█▄` |
| format_float | 19.8 | `▁████▇` |
| command/status | 1,341.7 | `▁████▄` |
| command/ping | 261.1 | `▁█▇▇▇▆` |
| command/shared | 229.9 | `▁▆█▆▆▅` |
| command/config_get | 4,485.7 | `█████▁` |
| command/ota_reject | 879.2 | `▁████▇` |
| command/unknown | 239.6 | `▁█▇▇▇▅` |
| command/parse_error | 221.9 | `▁███▇▆` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
| `c533430` | 2026-10-15 | 6/6 | 30/30 |
| `d1e2f98` | 2026-10-15 | 6/6 | 30/30 |
| `02371df` | 2026-10-15 | 6/6 | 30/30 |
| `47d88a7` | 2026-10-15 | 6/6 | 30/30 |
