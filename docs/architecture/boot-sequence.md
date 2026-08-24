# Boot sequence

Source: [`src/boot/boot_sequence.cpp`](../../src/boot/boot_sequence.cpp)

This file documents the ordering constraints that make the boot path work on a
no-PSRAM ESP32 WROOM. Re-ordering the early stages will OOM the largest
contiguous block needed by NimBLE, the LVGL pool, or `USB_RX_BUF_SIZE`, and the
device will brick at boot. Source: `src/boot/boot_sequence.cpp` (steady state)
and `src/main.cpp::setup` (pre-`BootSequence::run` reservations).

## The contiguous-block budget

On the production WROOM board, the largest free DRAM block after Arduino init
is ~120 KB. Several boot consumers each need a large contiguous chunk:

| Consumer                      | Reservation | Notes                                                   |
| ----------------------------- | ----------- | ------------------------------------------------------- |
| NimBLE early init             | ~50 KB      | `BleServer::earlyInit()` — fails ~16 KB after `lv_init` |
| LVGL pool (`lv_init`)         | ~80 KB      | once claimed, fragments the remainder                   |
| USB rxBuf (`USB_RX_BUF_SIZE`) | ~16 KB      | doubles as PUT_CONFIG receive buffer                    |
| TWAI driver init stack        | ~4 KB       | static stack pre-reserved in `CanManager`               |
| ArduinoJson dashboard parse   | ~20 KB      | runs during `ConfigLoader::loadAll`                     |

If any of these is requested AFTER `lv_init` has claimed its 80 KB pool, the
allocation fails because the residual heap has dropped below the requested
contiguous size. The recovery path on each failure is degraded — BLE silently
won't advertise, USB receive disables, CAN won't install — so the policy is
fail-loud at boot rather than silently degrade.

## Required ordering

```
setup()                                             // main.cpp
  ├─ Serial.begin                                   // UART up so logs land
  ├─ UsbComm::reserveRxBuf()                        // ~16 KB BEFORE lv_init
  ├─ CanManager::reserveInitTaskStack()             // ~4 KB BEFORE lv_init
  └─ BootSequence::run()                            // boot_sequence.cpp
       ├─ silenceNvsLogNoise                        // demote nvs ERR → WARN
       ├─ initPsramAndLogEntry                      // probe PSRAM if present
       ├─ initTaskWatchdog                          // WDT armed
       ├─ initBleEarlyIfEnabled                     // NimBLE ~50 KB
       ├─ mountStorageOrLogError                    // SPIFFS
       ├─ provisionDefaultConfigsIfNeeded           // first-boot embed→FS
       ├─ loadConfigWithHeapBracket                 // ArduinoJson ~20 KB
       ├─ initDisplayAndLVGL                        // lv_init claims ~80 KB
       │    └─ DisplayDriver::init + register
       ├─ initTouchHardware
       ├─ initLvglFsIfStorageOk                     // needs lv_init done
       ├─ provisionDefaultFontsIfNeeded             // before FontManager
       ├─ initFontManagerWithHeapLog
       ├─ preloadIconsWithHeapLog                   // SPIFFS reads while heap large
       ├─ showSplashWithInitialUpdates              // logo at full size from frame 1
       ├─ initRuntimeServices                       // TimerService → SignalStore → …
       ├─ initCanHardwarePhase                      // installs TWAI on reserved stack
       ├─ initUsbCommPhase
       ├─ buildUiWithHeapBracket                    // PageManager::init
       ├─ holdSplashUntilMin (2000 ms)              // user can read version
       └─ logBootCompleteAndReady                   // "[BOOT] Ready" marker
```

`setup()` calls `reserveRxBuf` and `reserveInitTaskStack` **before**
`BootSequence::run` so those allocations happen against the still-clean
post-Arduino heap.

## Why each ordering matters

- **`reserveRxBuf` / `reserveInitTaskStack` before `lv_init`**: After
  `lv_init` claims its 80 KB pool, the largest contiguous block on WROOM
  drops to ~13–18 KB. `USB_RX_BUF_SIZE` is `CONFIG_JSON_DOC_DASHBOARD +
256` ≈ 16 KB and the TWAI stack is ~4 KB — both at the cliff. PR #1374
  and #1376 fixed the boot OOM that surfaced when these moved after
  `lv_init`.

- **`initBleEarlyIfEnabled` before `initDisplayAndLVGL`**: NimBLE needs
  ~50 KB contiguous DRAM. After LovyanGFX init the largest free block
  shrinks to ~16 KB, making BLE impossible. The `BleServer::earlyInit()`
  path reserves NimBLE's stack while the heap is still large and
  unfragmented; subsequent BLE start/stop cycles reuse that arena rather
  than re-allocating.

- **`mountStorageOrLogError` + `loadConfigWithHeapBracket` before
  `lv_init`**: ArduinoJson's stream parser needs ~20 KB contiguous heap
  to parse `dashboard.json`. After `lv_init` the largest free block drops
  to ~15 KB, causing `NoMemory` parse failures. At this point the heap
  has ~120 KB contiguous — ample.

- **`lv_init()` before `DisplayDriver::init()`**: LovyanGFX's
  `s_lcd.init()` fragments the heap such that LVGL's pool malloc no
  longer fits. `lv_init` first; display registration afterwards.

- **`initLvglFsIfStorageOk` after `lv_init`**: registers `lv_fs_drv` —
  the call only makes sense once LVGL has booted.

- **`provisionDefaultFontsIfNeeded` before `initFontManagerWithHeapLog`**:
  FontManager's `lv_font_load()` opens SPIFFS paths; the embedded
  `.bin` fonts must already be staged on the filesystem.

- **`preloadIconsWithHeapLog` before page widgets allocate**: page-widget
  allocations consume the same LVGL pool as the SPIFFS icon decoder. After
  `PageManager::init` runs, the largest free block drops below the
  FS-open threshold (`LVGL_FS_MIN_HEAP_BYTES`) and on-demand icon loads
  fail (#956). Preloaded entries live in the LVGL image cache
  (`LV_IMG_CACHE_DEF_SIZE`) and survive page rebuilds + theme toggles
  without re-touching SPIFFS.

- **`updateSplash("Ready", 100)` before `buildUI()`**: `PageManager::init`
  calls `lv_obj_clean(lv_scr_act())` which frees the splash objects.
  Any call to `updateSplash` after that point would deref freed objects.

- **`holdSplashUntilMin` uses a yielding `vTaskDelay` loop**: boot tends
  to finish in <1 s, which feels twitchy and gives the user no time to
  read the version. The 2 s hold is required UX. Uses `vTaskDelay` rather
  than the Arduino `delay()` shim so the scheduler keeps running other
  tasks during the hold and the granularity is visible to readers (#1207).

## OTA mark-valid placement

`BootSequence::markOtaSlotValidIfPending()` is **NOT** called from
`BootSequence::run()`. It is called from `taskUI` once
`UI_OTA_VALID_FRAMES` successful frames have rendered (`main.cpp`).

The original placement (right after `[BOOT] Ready`) marked too early — a
crash inside the first `lv_task_handler()` call (font decode, page
rebuild, theme apply) happens AFTER the mark and therefore never triggers
the bootloader rollback. F-ME-8 (#1014) moved the mark to a gated point
inside taskUI so a flaky build self-rolls-back. Source: `main.cpp`
`taskUI` loop.

## CAN hardware init: tolerate failure

`CanManager::initHardware()` returns an `esp_err_t`. Boot continues even
on failure so the UI and USB remain usable for config edits; the
in-driver retry loop in `can_manager.cpp::tick()` keeps re-attempting
installation. The failure is surfaced through `ErrorStore` so the top bar
and error bar can show it instead of silently reading 0 fps forever (#1224).

## "[BOOT] Ready" marker

The CI smoke test
(`.github/workflows/firmware-boot-smoke.yml`, #486) asserts that
`[BOOT] Ready` appears exactly once at the end of a successful boot. Do
not move, duplicate, or reword the line without updating the workflow.
