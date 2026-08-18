# Board profile

A `BoardProfile` describes one dash: which LCD controller, which touch controller,
which pins, which CAN speed, how much SPIFFS. Everything hardware-shaped that the
firmware needs but cannot discover reads from it.

It is resolved **at run time**. The build flag picks a default, not the only option.

## Where a profile comes from

Three sources, in the order the firmware tries them at boot
(`BoardProfileStore::loadAndApply()`):

| Order | Source                         | Stored as                                              |
| ----- | ------------------------------ | ------------------------------------------------------ |
| 1     | A catalog board selected by id | NVS `boardcfg/boardid` — a short string                |
| 2     | A full profile blob            | NVS `boardcfg/profile` — the `CANSHIFT_BOARD` envelope |
| 3     | The compile-time default       | the `BOARD_*` build flag                               |

The first two are mutually exclusive: writing one clears the other, so there is no
question of which wins after a re-provision.

Whatever wins is copied into a single mutable profile. Everything downstream reads
`canshift::boards::runtimeBoardProfile()` — never the compile-time constant.

## The catalog

Every board header under `include/boards/` defines its own profile symbol, and
`include/boards/catalog.h` gathers all of them into `kCatalog`. The whole catalog
is compiled into every binary; it costs about **1 KB of flash for five boards**.

`catalogBoard(id, chipFamily)` looks one up. The chip family is not decoration: an
ESP32 binary must refuse an ESP32-S3 profile, because the pins would be applied to
a chip that cannot honour them. A lookup that crosses families returns `nullptr`
and the runtime profile is left alone.

## Provisioning over USB

`CMD_SET_BOARD_PROFILE` (`0x0D`) accepts **two payload shapes**, distinguished by
whether the payload carries a `magic` field:

```jsonc
// 1. Pick a board this firmware already knows
{ "cmd": 13, "payload": { "board_id": "waveshare_s3_28" } }

// 2. Provision a board it does not — the full CANSHIFT_BOARD envelope
{ "cmd": 13, "payload": { "magic": "CANSHIFT_BOARD", "schema": "board-profile",
                          "formatVersion": 1, "profile": { ... } } }
```

Both reply `{"status":"ok","restart":true}` and reboot, because the display driver
is configured once at init. Errors: `unknown_board_id` when the id is absent from
this build's catalog or belongs to another chip family, `invalid_board_profile`
when a blob fails validation, `blob_too_large` past 1536 bytes.

**Prefer the id form.** It is a few dozen bytes instead of one and a half kilobytes,
it cannot describe a board this firmware has no driver for, and it is validated
against the catalog before anything is written to NVS. The blob form exists for
boards outside the catalog — a custom build, a panel we do not ship.

`GET_STATUS` reports `board_id` from the **runtime** profile, so it reflects what
was provisioned rather than what the binary was built with.

## What the profile cannot describe

`LcdProfile` carries SPI pins and one write frequency. RGB-parallel panels need a
data bus, sync lines and porch timings, so they are not expressible as a profile
variant today — see #51.

A profile may only name drivers this build can construct; see
[Board drivers](../reference/board-drivers.md) for the matrix and for what happens
when it names one that is missing.
