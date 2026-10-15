# CANShift firmware bench

Latest: `47d88a7` on 2026-10-15, 1 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,272,496 | `▅` |
| crowpanel_28 | dram | 89,644 | `▅` |
| crowpanel_28 | iram | 110,327 | `▅` |
| crowpanel_28 | psram | 0 | `▅` |
| generic_esp32s3 | flash | 1,185,248 | `▅` |
| generic_esp32s3 | dram | 83,400 | `▅` |
| generic_esp32s3 | iram | 77,959 | `▅` |
| generic_esp32s3 | psram | 0 | `▅` |
| generic_ili9341 | flash | 1,272,672 | `▅` |
| generic_ili9341 | dram | 89,644 | `▅` |
| generic_ili9341 | iram | 110,327 | `▅` |
| generic_ili9341 | psram | 0 | `▅` |
| generic_ili9341_gt911 | flash | 1,272,960 | `▅` |
| generic_ili9341_gt911 | dram | 89,644 | `▅` |
| generic_ili9341_gt911 | iram | 110,327 | `▅` |
| generic_ili9341_gt911 | psram | 0 | `▅` |
| waveshare_s3_28 | flash | 1,632,112 | `▅` |
| waveshare_s3_28 | dram | 104,844 | `▅` |
| waveshare_s3_28 | iram | 88,963 | `▅` |
| waveshare_s3_28 | psram | 0 | `▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 55.6 | `▅` |
| can_decode | 124.0 | `▅` |
| expr_eval | 22.3 | `▅` |
| telemetry_encode | 34.4 | `▅` |
| status_writer | 736.2 | `▅` |
| format_float | 12.8 | `▅` |
| command/status | 910.3 | `▅` |
| command/ping | 179.5 | `▅` |
| command/shared | 163.5 | `▅` |
| command/config_get | 7,015.6 | `▅` |
| command/ota_reject | 529.2 | `▅` |
| command/unknown | 160.8 | `▅` |
| command/parse_error | 155.5 | `▅` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `47d88a7` | 2026-10-15 | 6/6 | 30/30 |
