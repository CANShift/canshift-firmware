# CANShift firmware bench

Latest: `4f2d0d8` on 2026-10-16, 10 commits recorded. Trends cover the last 30 commits on `main`; the raw data is `history.json`.

## Image sizes

| board | metric | bytes | trend |
| --- | --- | ---: | --- |
| crowpanel_28 | flash | 1,274,256 | `▁▁▁▁▁▁▁▁▇█` |
| crowpanel_28 | dram | 89,964 | `▁▁▁▁▁▁▁▁██` |
| crowpanel_28 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅` |
| crowpanel_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | flash | 1,187,056 | `▁▁▁▁▁▁▁▁▇█` |
| generic_esp32s3 | dram | 83,720 | `▁▁▁▁▁▁▁▁██` |
| generic_esp32s3 | iram | 77,959 | `▅▅▅▅▅▅▅▅▅▅` |
| generic_esp32s3 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | flash | 1,274,448 | `▁▁▁▁▁▁▁▁▇█` |
| generic_ili9341 | dram | 89,964 | `▁▁▁▁▁▁▁▁██` |
| generic_ili9341 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | flash | 1,274,736 | `▁▁▁▁▁▁▁▁▇█` |
| generic_ili9341_gt911 | dram | 89,964 | `▁▁▁▁▁▁▁▁██` |
| generic_ili9341_gt911 | iram | 110,327 | `▅▅▅▅▅▅▅▅▅▅` |
| generic_ili9341_gt911 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | flash | 1,633,936 | `▁▁▁▁▁▁▁▁▇█` |
| waveshare_s3_28 | dram | 105,164 | `▁▁▁▁▁▁▁▁██` |
| waveshare_s3_28 | iram | 88,963 | `▅▅▅▅▅▅▅▅▅▅` |
| waveshare_s3_28 | psram | 0 | `▅▅▅▅▅▅▅▅▅▅` |

## Hot paths

| case | median ns | trend (÷ calibration) |
| --- | ---: | --- |
| calibration | 72.8 | `▅▅▅▅▅▅▅▅▅▅` |
| can_decode | 176.3 | `▁████▅▄█▄█` |
| expr_eval | 49.5 | `▁█▇██▆▆█▆█` |
| telemetry_encode | 66.2 | `▁▇▆▇▇██▇▆▇` |
| status_writer | 1,517.8 | `▁█▇▇█▄▄█▆█` |
| format_float | 23.1 | `▁████▇██▄█` |
| command/status | 1,848.7 | `▁████▄▄█▆█` |
| command/ping | 331.5 | `▁▇▆▆▆▅▅▆▂█` |
| command/shared | 260.5 | `▂▆█▆▆▅▆▆▁▆` |
| command/config_get | 9,225.5 | `▇▇▇▇▇▁▁▇█▇` |
| command/ota_reject | 1,031.8 | `▁████▇▇█▄█` |
| command/unknown | 292.2 | `▁█▇▇▇▅▅▇▂▇` |
| command/parse_error | 271.6 | `▂███▇▆▆█▁█` |

## Goldens

| commit | date | behaviour | visual |
| --- | --- | ---: | ---: |
| `4f2d0d8` | 2026-10-16 | 6/6 | 30/30 |
| `1703a03` | 2026-10-15 | 6/6 | 30/30 |
| `51c8a9b` | 2026-10-15 | 6/6 | 30/30 |
| `f17c9e1` | 2026-10-15 | 6/6 | 30/30 |
| `59df1cf` | 2026-10-15 | 6/6 | 30/30 |
| `2ef5522` | 2026-10-15 | 6/6 | 30/30 |
| `c533430` | 2026-10-15 | 6/6 | 30/30 |
| `d1e2f98` | 2026-10-15 | 6/6 | 30/30 |
| `02371df` | 2026-10-15 | 6/6 | 30/30 |
| `47d88a7` | 2026-10-15 | 6/6 | 30/30 |
