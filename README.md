# CANShift firmware bench

Latest: `d1e2f98` on 2026-10-15, 3 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,272,496 | `▅▅▅` |
| crowpanel_28 | dram | 89,644 | `▅▅▅` |
| crowpanel_28 | iram | 110,327 | `▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅` |
| generic_esp32s3 | flash | 1,185,248 | `▅▅▅` |
| generic_esp32s3 | dram | 83,400 | `▅▅▅` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅` |
| generic_ili9341 | flash | 1,272,672 | `▅▅▅` |
| generic_ili9341 | dram | 89,644 | `▅▅▅` |
| generic_ili9341 | iram | 110,327 | `▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅` |
| generic_ili9341_gt911 | flash | 1,272,960 | `▅▅▅` |
| generic_ili9341_gt911 | dram | 89,644 | `▅▅▅` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅` |
| waveshare_s3_28 | flash | 1,632,112 | `▅▅▅` |
| waveshare_s3_28 | dram | 104,844 | `▅▅▅` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 72.9 | `▅▅▅` |
| can_decode | 176.3 | `▁██` |
| expr_eval | 48.0 | `▁██` |
| telemetry_encode | 65.4 | `▁██` |
| status_writer | 1,488.2 | `▁██` |
| format_float | 23.1 | `▁██` |
| command/status | 1,847.4 | `▁██` |
| command/ping | 304.5 | `▁█▇` |
| command/shared | 289.5 | `▁▆█` |
| command/config_get | 9,201.5 | `▁█▁` |
| command/ota_reject | 1,029.8 | `▁██` |
| command/unknown | 293.6 | `▁█▇` |
| command/parse_error | 269.3 | `▁██` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `d1e2f98` | 2026-10-15 | 6/6 | 30/30 |
| `02371df` | 2026-10-15 | 6/6 | 30/30 |
| `47d88a7` | 2026-10-15 | 6/6 | 30/30 |
