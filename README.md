# CANShift firmware bench

Latest: `1703a03` on 2026-10-15, 9 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,274,112 | `▁▁▁▁▁▁▁▁█` |
| crowpanel_28 | dram | 89,964 | `▁▁▁▁▁▁▁▁█` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,186,912 | `▁▁▁▁▁▁▁▁█` |
| generic_esp32s3 | dram | 83,720 | `▁▁▁▁▁▁▁▁█` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,274,288 | `▁▁▁▁▁▁▁▁█` |
| generic_ili9341 | dram | 89,964 | `▁▁▁▁▁▁▁▁█` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,274,592 | `▁▁▁▁▁▁▁▁█` |
| generic_ili9341_gt911 | dram | 89,964 | `▁▁▁▁▁▁▁▁█` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,633,776 | `▁▁▁▁▁▁▁▁█` |
| waveshare_s3_28 | dram | 105,164 | `▁▁▁▁▁▁▁▁█` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 81.7 | `▅▅▅▅▅▅▅▅▅` |
| can_decode | 188.3 | `▁████▅▄█▄` |
| expr_eval | 47.6 | `▁█▇██▆▆█▆` |
| telemetry_encode | 72.7 | `▁▇▆▇▇██▇▆` |
| status_writer | 1,568.7 | `▁█▇▇█▄▄█▆` |
| format_float | 21.8 | `▁████▇██▄` |
| command/status | 1,891.5 | `▁████▄▄█▆` |
| command/ping | 271.7 | `▁█▇▇▇▆▆▇▂` |
| command/shared | 232.0 | `▂▆█▆▆▅▆▆▁` |
| command/config_get | 11,288.8 | `▇▇▇▇▇▁▁▇█` |
| command/ota_reject | 916.6 | `▁████▇▇█▄` |
| command/unknown | 249.6 | `▁█▇▇▇▅▅▇▂` |
| command/parse_error | 218.7 | `▂███▇▆▆█▁` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
| `c533430` | 2026-10-15 | 6/6 | 30/30 |
| `d1e2f98` | 2026-10-15 | 6/6 | 30/30 |
| `02371df` | 2026-10-15 | 6/6 | 30/30 |
| `47d88a7` | 2026-10-15 | 6/6 | 30/30 |
