# Flash layout

Every board build carries two app slots, so a firmware update is written to the
slot the dash is not running from and the bootloader swaps them on the next boot.
What changes between boards is how much flash there is around those slots.

## The partition tables

| Partition  | `ota_4mb.csv` (4 MB boards) | `ota_16mb.csv` (Waveshare S3) | Holds                                                 |
| ---------- | --------------------------- | ----------------------------- | ----------------------------------------------------- |
| `nvs`      | `0x9000`, 20 KB             | `0x9000`, 20 KB               | Settings, touch calibration, BLE bonds, board profile |
| `otadata`  | `0xe000`, 8 KB              | `0xe000`, 8 KB                | Which app slot is active                              |
| `app0`     | `0x10000`, 1536 KB          | `0x10000`, 4096 KB            | Firmware slot 0                                       |
| `app1`     | `0x190000`, 1536 KB         | `0x410000`, 4096 KB           | Firmware slot 1                                       |
| `spiffs`   | `0x310000`, 832 KB          | `0x810000`, 7552 KB           | Config, fonts, icons                                  |
| `coredump` | `0x3E0000`, 128 KB          | `0xF70000`, 128 KB            | Panic dumps                                           |

The bootloader sits below `0x8000` and the partition table at `0x8000` on both.
Which table a board uses is `board_build.partitions` in its env in
[`platformio.ini`](../../platformio.ini): `waveshare_s3_28` takes `ota_16mb.csv`,
every other env inherits `ota_4mb.csv`.

`[env:secure]` uses [`ota_4mb_secure.csv`](../../ota_4mb_secure.csv): the same app
and SPIFFS offsets, an `encrypted` flag on the data partitions, and a 4 KB
`nvs_keys` partition carved out of `nvs` for encrypted NVS. See
[Secure boot setup](secure-boot-setup.md).

Changing a partition table is a USB reflash, never an OTA. The running bootloader
keeps the old table, so an OTA image built for a new layout lands in a slot whose
offset no longer matches and SPIFFS ends up in the wrong place.

## Why workshop mode needs 16 MB

[Workshop mode](../architecture/workshop-mode.md) serves the tuner from the dash
over a Wi-Fi access point. Three budgets decide which boards can carry it, and the
4 MB boards miss all three:

| Budget   | 4 MB ESP32 (CrowPanel, generic)                                                                  | 16 MB ESP32-S3 (Waveshare)          |
| -------- | ------------------------------------------------------------------------------------------------ | ----------------------------------- |
| App slot | The image already fills about 90 % of 1536 KB, with no room for a Wi-Fi stack and an HTTP server | 4096 KB, about 31 % used            |
| SPIFFS   | 832 KB shared with config, fonts and icons                                                       | 7552 KB, room for the gzipped tuner |
| RAM      | No PSRAM on the WROOM module; the largest free block after `lv_init` is about 13–18 KB           | 8 MB PSRAM                          |

Wi-Fi and BLE share one radio, and the access point plus its server need heap that
a no-PSRAM WROOM does not have once LVGL owns its pool
([Boot sequence](../architecture/boot-sequence.md)). Shrinking to one app slot to
make room would cost OTA updates and their rollback.

So the 4 MB boards keep USB as their only configuration link: the tuner over a cable,
firmware updates with `CMD_OTA_*` over USB, and the mobile app over BLE while
driving. They get no workshop button.

## What lives in SPIFFS

| Path                          | Contents                                                |
| ----------------------------- | ------------------------------------------------------- |
| `/config/dashboard.json`      | Pages, widgets, layout, theme                           |
| `/config/signals.json`        | CAN signal mapping                                      |
| `/config/device.json`         | CAN speed; TWAI pins only to override the board profile |
| `/config/input_bindings.json` | [Physical buttons](physical-buttons.md), optional       |
| `/fonts/`                     | LVGL binary fonts                                       |
| `/assets/`                    | LVGL binary icons                                       |

The schemas of the four config files belong to `@canshift/core`:
[config contract](https://github.com/CANShift/canshift-core/blob/main/docs/config-contract.md).
The image released as `-spiffs.bin` is the [`data/`](../../data/) folder, built by
`pio run -e <env> -t buildfs`; CI unpacks it and checks every file against `data/`.

### First boot without a SPIFFS image

The firmware still boots. It formats a SPIFFS partition that does not mount, then
writes back what it carries inside the app image: `dashboard.json`
([`default_config.cpp`](../../src/config/default_config.cpp)) and the fonts listed
under `board_build.embed_files`. Files that already exist are never overwritten.
The image stores each of them deflated: the build
([`deflate_embeds.py`](../../scripts/deflate_embeds.py)) writes a 4-byte length plus
a zlib stream with a 4 KB window, and checks the round trip. The firmware inflates
it with the tinfl in the chip ROM ([`embedded_asset.cpp`](../../src/boot/embedded_asset.cpp)),
streaming through a 4 KB ring into an atomic chunked write, so provisioning needs
about 15 KB of heap whatever the file size. The zlib Adler-32 and the length are
checked before the write is committed: a damaged copy reports `EMBED_CORRUPT` and
leaves nothing on SPIFFS.
Icons have no embedded copy, so flash the `-spiffs.bin` too.

### Atomic writes and the `.bak` fallback

Config writes go through `StorageDriver::writeFileAtomic`, which keeps a
`<file>.bak` companion. At boot, `readAndParseWithBak`
([`json_helpers.inc`](../../src/config/json_helpers.inc)) falls back to the `.bak`
when the primary file is missing or fails to parse, so an interrupted burn costs
the last change rather than the whole config.
