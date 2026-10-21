# Boot sequence

Source: [`src/boot/boot_sequence.cpp`](../../src/boot/boot_sequence.cpp)
(`BootSequence::run`), [`src/boot/boot_phases.cpp`](../../src/boot/boot_phases.cpp)
(the individual phases) and `src/main.cpp::setup` (the reservations made
before `BootSequence::run`).

This file documents the ordering constraints of the boot path. Most of them
exist for boards without PSRAM, where every large consumer competes for
contiguous internal DRAM: re-ordering the early stages starves NimBLE, the
LVGL pool or the USB receive buffer, and the device boots degraded or not at
all.

## The contiguous-block budget

| Consumer                              | Reservation                                    | Notes                                                                |
| ------------------------------------- | ---------------------------------------------- | -------------------------------------------------------------------- |
| Task stacks (`preallocateTaskStacks`) | `TASK_STACK_UI`, `_USB`, `_BLE` + the CAN task | allocated first; boot halts if any of them fails                     |
| USB rxBuf (`USB_RX_BUF_SIZE`)         | ~16 KB                                         | doubles as the PUT_CONFIG receive buffer                             |
| TWAI driver init stack                | ~4 KB                                          | pre-reserved in `CanManager`                                         |
| LVGL pool (`lv_init`)                 | 512 KB PSRAM, else up to 80 KB internal        | sized at run time, see below                                         |
| ArduinoJson dashboard parse           | ~20 KB, transient                              | runs during `ConfigLoader::loadAll`                                  |
| NimBLE early init                     | `kBleMinHeapBytes` (50 KB) contiguous          | `BleServer::earlyInit()` skips BLE when the largest block is smaller |

The LVGL pool is claimed on LVGL's first allocation, inside `lv_init()`
(`src/boot/lvgl_pool_alloc.cpp`). It takes 512 KB of PSRAM when the board has
it. Without PSRAM it takes the largest internal block minus 16 KB of
headroom, capped at 80 KB, and halves the request down to 32 KB if the
allocation fails. If not even 32 KB is free, the device restarts so a pending
OTA image can roll back.

## Required ordering

```
setup()                                             // main.cpp
  ├─ Serial.begin, Logger::init
  ├─ LVGL mutex
  ├─ preallocateTaskStacks()                        // UI, CAN, USB, BLE stacks
  ├─ UsbComm::reserveRxBuf()                        // ~16 KB before lv_init
  ├─ CanManager::reserveInitTaskStack()             // ~4 KB before lv_init
  ├─ BootSequence::run()                            // boot_sequence.cpp
  │    ├─ silenceFrameworkLogNoise                  // nvs ERR → WARN; release: ESP-IDF logs off
  │    ├─ BoardProfileStore::loadAndApply           // provisioned board, else the build default
  │    ├─ WorkshopMode::takeResumeRequest           // reads and disarms the one-shot resume flag
  │    ├─ initPsramAndLogEntry                      // probe PSRAM if present
  │    ├─ initTaskWatchdog
  │    ├─ initLvglMemoryPool                        // lv_init claims the LVGL pool
  │    ├─ loadStorageAndConfig
  │    │    ├─ mountStorageOrLogError               // SPIFFS
  │    │    ├─ provisionDefaultConfigsIfNeeded      // first-boot embed → FS
  │    │    ├─ loadConfigWithHeapBracket            // ArduinoJson ~20 KB
  │    │    ├─ ScreenProfile::initFromDashboard
  │    │    └─ initBleEarlyIfEnabled                // NimBLE ≥ 50 KB contiguous
  │    ├─ initDisplayHardware                       // DisplayDriver::init + registerWithLVGL
  │    ├─ initTouchHardware                         // controller probed before calibration
  │    ├─ prepareUiAssets
  │    │    ├─ initLvglFsIfStorageOk
  │    │    ├─ provisionDefaultFontsIfNeeded        // before FontManager
  │    │    ├─ initFontManagerWithHeapLog
  │    │    └─ preloadIconsWithHeapLog              // SPIFFS reads before page widgets
  │    ├─ showBootScreen                            // D01 boot screen, then backlight on
  │    ├─ initRuntimeServices                       // TimerService → SignalStore → TrackStore → AlertEngine
  │    ├─ initCanHardwarePhase                      // installs TWAI on the reserved stack
  │    ├─ initUsbCommPhase
  │    ├─ startCanTaskAndListen                     // taskCAN runs; waits ≤ 1 s for a frame
  │    ├─ showSelfTestScreen                        // D02 self-test, after ≥ 400 ms of boot screen
  │    ├─ buildUiWithHeapBracket                    // after ≥ 2 s of self-test; PageManager::init
  │    ├─ WorkshopMode::resumeAfterReboot           // re-enters workshop mode when armed
  │    └─ logBootCompleteAndReady                   // "[BOOT] Ready" marker
  ├─ createAllTasks()                               // ui, usb, ble on the preallocated stacks
  ├─ InputButtons::init()
  └─ vTaskDelete(nullptr)
```

`setup()` runs on the Arduino `loopTask`, whose 16 KB stack
(`ARDUINO_LOOP_STACK_SIZE`) the boot path needs. Once the tasks are started,
`setup()` deletes its own task: `loop()` has no work, and the idle task
returns the whole stack to the heap.

## Why each ordering matters

- **Task stacks, `reserveRxBuf` and `reserveInitTaskStack` before
  `BootSequence::run`**: they are allocated against the still-clean
  post-Arduino heap. `USB_RX_BUF_SIZE` is `CONFIG_JSON_DOC_DASHBOARD + 256`
  ≈ 16 KB and the TWAI stack ~4 KB; once the LVGL pool is claimed on a
  board without PSRAM, neither fits any more.

- **`lv_init` before storage and config**: the pool is sized from the
  largest free block at that moment. Mounting SPIFFS and parsing the
  dashboard first would fragment the heap and shrink the pool.

- **Config before BLE**: the transient `JsonDocument` for a full dashboard
  needs its ~20 KB while NimBLE has not yet claimed its share of internal
  heap. `JsonReader` allocates documents from PSRAM when the board has it
  (a 35 KB dashboard parses to about three times that) and falls back to
  internal RAM otherwise. Button parameters (888 B) live out of line in
  `CfgWidgetParams`: inline, every widget would cost 1 KB and the six-page
  default would overflow a WROOM heap.

- **One LVGL lock for the whole page build**: `g_lvglMutex` is not recursive,
  and every step of the build calls `lv_*`; taking it per call is what once
  left `init()` unguarded.

- **`initBleEarlyIfEnabled` before `initDisplayHardware`**: NimBLE needs
  `kBleMinHeapBytes` of contiguous DRAM, and LovyanGFX's panel init
  fragments the heap. Early init reserves NimBLE while a block that size
  still exists; later start/stop cycles reuse it.

- **The display stays separate from `lv_init`**: LovyanGFX's `s_lcd.init()`
  fragments the heap, so the pool is claimed first and the display is
  registered with LVGL afterwards.

- **Board profile first**: `BoardProfileStore::loadAndApply` selects the
  pins, panel and touch controller every later phase uses.

- **Workshop resume read at the start, applied at the end**: the resume
  flag is disarmed before anything can crash, so a boot that fails does not
  loop back into workshop mode; it is applied only once the dashboard is
  built ([Workshop mode](workshop-mode.md#entering-and-leaving)).

- **The DISPLAY and TOUCH rows are measured**: `DisplayDriver::init()`
  returns whether the draw buffers were allocated and `s_lcd.init()`
  succeeded (one retry after 50 ms). `TouchDriver::init()` re-runs the
  controller's own `init()` up to three times, 20 ms apart, before anything
  depends on it. A controller that never answers pushes `TOUCH_NONE`, and the
  first-boot calibration is skipped: `calibrateTouch()` waits for taps that
  would never come. The XPT2046 has no ID register and shares the panel's
  SPI bus, so it is not re-initialised and reads as answering.

- **Backlight after the first frame**: the brightness is set to 0 before
  `s_lcd.init()`, which applies it, and `DisplayDriver::backlightOn()` runs
  after the boot screen's first `lv_refr_now()`, so the panel never shows
  its power-on garbage.

- **`initLvglFsIfStorageOk` after `lv_init`**: registers `lv_fs_drv` —
  the call only makes sense once LVGL has booted.

- **`provisionDefaultFontsIfNeeded` before `initFontManagerWithHeapLog`**:
  FontManager's `lv_font_load()` opens SPIFFS paths; the embedded
  `.bin` fonts must already be staged on the filesystem. A face is
  rewritten when its file is missing **or** when the stamp NVS holds for
  it (namespace `fonts`) differs from the firmware's copy. The stamp is the
  Adler-32 in the deflated blob's zlib trailer, so a font changed by an OTA
  reaches SPIFFS on the first boot of the new image. The stamp is recorded
  only after the file is written and committed. If recording it fails, the
  face is rewritten again on the next boot.

- **`preloadIconsWithHeapLog` before page widgets allocate**: page-widget
  allocations consume the same LVGL pool as the SPIFFS icon decoder. After
  `PageManager::init` runs, the largest free block drops below the
  FS-open threshold (`kLvglFsMinHeapBytes`) and on-demand icon loads
  fail (#956). Preloaded entries live in the LVGL image cache
  (`LV_IMG_CACHE_DEF_SIZE`) and survive page rebuilds + theme toggles
  without re-touching SPIFFS.

- **`startCanTaskAndListen` before `showSelfTestScreen`**: the CAN BUS
  row reports whether a frame arrived, and only `taskCAN` drains the TWAI
  queue and stamps the last-RX time. The task starts here, not with the
  others in `createAllTasks()`, and boot waits up to `kCanListenWindowMs`
  for the first frame. With a live bus that ends within milliseconds;
  without one it adds up to the window to boot, which the self-test needs
  to say anything true. Started later, the row always read NO FRAMES and
  the 32-deep RX queue overflowed during the holds.

- **Boot screen and self-test holds**: boot tends to finish in under a
  second. The boot screen stays up at least `kBootHoldMs` (400 ms) and the
  self-test at least `kSelfTestHoldMs` (2 s) so the version and the check
  rows can be read. The holds use `vTaskDelay`, so the scheduler keeps
  running other tasks.

- **`showSelfTestScreen` and `buildUI` clean the screen first**: each calls
  `lv_obj_clean(lv_scr_act())`, which frees the previous screen's objects;
  nothing may touch them afterwards.

## OTA mark-valid placement

A freshly OTA-written slot boots as `ESP_OTA_IMG_PENDING_VERIFY`, and the
prebuilt bootloader rolls back to the previous slot if it resets before the
image is marked valid. The mark has one home:
`BootSequence::markOtaSlotValidIfPending()`, and `OtaSoak::marksValid`
(`runtime/ota_soak.h`) decides when it runs. `taskUI` calls it on the first dash
frame after 60 s of uptime. So a crash anywhere in `setup()`, the boot sequence
or the first minute of driving rolls back. Workshop frames never count: an
update that resumes workshop mode
([Workshop mode](workshop-mode.md#entering-and-leaving)) has not drawn a single
page. It is marked valid when the driver leaves workshop mode
(`WorkshopMode::restartIntoDash`), if the image has been up for 60 s by then.
Leaving it earlier, or the device losing power in workshop mode, rolls the
update back. The gap that remains: an image delivered over workshop
Wi-Fi is validated before its dash render path has drawn a page, so a crash
there is no longer rolled back and needs a USB reflash.

arduino-esp32's `initArduino()` would otherwise mark the image valid itself,
before `setup()` runs: it calls the weak `verifyRollbackLater()`, which returns
`false` by default, and then `esp_ota_mark_app_valid_cancel_rollback()`.
`boot_sequence.cpp` defines `verifyRollbackLater()` to return `true`, which hands
the mark to the gate above (#312). Without that override the gate never sees
`PENDING_VERIFY` and no crash after an OTA ever rolls back.

The gate sits after `kUiOtaValidFrames` rather than right after
`[BOOT] Ready` so that a crash inside the first `lv_task_handler()` calls
(font decode, page rebuild, theme apply) still rolls back (#1014).

## CAN hardware init: tolerate failure

`CanManager::initHardware()` returns an `esp_err_t`. Boot continues even
on failure so the UI and USB remain usable for config edits; the
in-driver retry loop in `can_manager.cpp::tick()` keeps re-attempting
installation. The failure is surfaced through `ErrorStore` so the top bar
and error bar can show it instead of silently reading 0 fps forever (#1224).

## "[BOOT] Ready" marker

`[BOOT] Ready` is logged exactly once, at the end of a successful boot, in builds
that log at `info` or above (release builds log errors only). It is
the line to look for in a boot log. No CI job asserts it any more: the QEMU boot
smoke test of the monorepo did not move to this repository.
