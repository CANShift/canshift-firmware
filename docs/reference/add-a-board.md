# Adding a board

There are two ways to run CANShift on hardware it does not know about, and the
first one needs no firmware change at all.

## Which path

|                      | Provision a profile                                    | Add it to the catalog        |
| -------------------- | ------------------------------------------------------ | ---------------------------- |
| Rebuild the firmware | no                                                     | yes                          |
| Reaches other users  | no                                                     | yes                          |
| Good for             | your own wiring, a one-off, bring-up                   | a board we intend to support |
| Constraint           | its LCD and touch controllers must already be built in | same                         |

Both paths hit the same wall if the panel needs a driver the binary does not
carry — see [Board drivers](board-drivers.md) for the matrix and what happens when
a profile names one that is missing.

## Path 1 — provision a profile over USB

The firmware resolves its board at run time. Send a `CANSHIFT_BOARD` envelope with
`CMD_SET_BOARD_PROFILE` (`0x0D`) and it persists to NVS and reboots into it:

```jsonc
{ "cmd": 13, "payload": { "magic": "CANSHIFT_BOARD", "schema": "board-profile",
                          "formatVersion": 1,
                          "profile": { "board_id": "my_rig", "board_name": "My rig",
                                       "chip_family": "esp32",
                                       "lcd": { "driver": "ILI9341", "pin_mosi": 13, ... },
                                       "touch": { ... }, "can": { ... } } } }
```

`@canshift/core` builds and validates that envelope — `serializeBoardProfile()` —
so the tuner is the intended way to produce it rather than hand-writing JSON.

The full field list is [`include/board_profile.h`](../../include/board_profile.h);
resolution order and the smaller by-id form are in
[Board profile](../architecture/board-profile.md).

## Path 2 — add it to the catalog

Five steps, and a gate that checks you did all of them. `scripts/check_board_lists.py`
runs in `lint` and fails the PR if any list disagrees, so a half-added board cannot
merge.

1. **Profile header** — copy an existing header in
   [`include/boards/`](../../include/boards/) to `include/boards/<board_id>.h`.
   The filename, the `.board_id` field and the profile symbol all have to agree:
   `waveshare_s3_28.h` declares `kWaveshareS328` with `.board_id = "waveshare_s3_28"`.
   Fields are named — no positional initialisers.

2. **Catalog** — add the symbol to `kCatalog` in
   [`include/boards/catalog.h`](../../include/boards/catalog.h). A board that is not
   in the catalog cannot be selected by id at run time, only baked in as the default.

3. **Default selector** — add an `#elif defined(BOARD_<BOARD_ID>)` arm in
   [`include/board.h`](../../include/board.h). This picks the _default_ profile when
   NVS holds nothing, not the only one the binary can run.

4. **Build env** — add `[env:<board_id>]` in
   [`platformio.ini`](../../platformio.ini), extending `env:crowpanel_28`,
   unflagging `-DBOARD_CROWPANEL_28=1` and flagging `-DBOARD_<BOARD_ID>=1`. The env
   name must equal the board id.

5. **Board list** — add an entry to
   [`.github/boards.json`](../../.github/boards.json) with `id`, `chip`, `display`,
   `touch` and `release`. CI builds every entry; only `release: true` entries get
   published artifacts and appear in `manifest.json`.

   Set `release: false` if the profile is a compile target rather than real hardware
   — `generic_esp32s3` has every pin at `-1` and would hand a user a dark screen.

6. **Core catalog** — a releasable board must also exist in `@canshift/core`'s
   `BOARD_PROFILES`, or the tuner cannot offer it. Core's own test enforces the
   reverse direction: its catalog must equal this repo's releasable set.

Then `pio run -e <board_id>` and `python3 scripts/check_board_lists.py`.

## Drivers

No display code is needed if the LCD and touch controllers are already compiled in.
The factory instantiates them from the profile's `lcd.driver` / `touch.driver` enums
at boot. Adding a controller means a `Panel_*` or `Touch_*` member in
[`include/lgfx_factory.h`](../../include/lgfx_factory.h), a `selectPanel` /
`selectTouch` arm, and a row in [Board drivers](board-drivers.md).

Neither switch has a `default:` arm, so the compiler will point at the new enum
value if you add one without wiring it.

## What a board profile cannot describe

RGB-parallel panels. `LcdProfile` carries SPI pins and one write frequency — no data
bus, sync lines or porch timings. That tier is a variant of the profile type rather
than another entry in the catalog (#51).
