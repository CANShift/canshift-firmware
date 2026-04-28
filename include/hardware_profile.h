#pragma once
// hardware_profile.h — Board capability declarations
//
// This file describes WHAT the board can do, not HOW (pin numbers are in board_config.h).
// Use these macros to conditionally compile hardware-dependent code paths.

// ---------------------------------------------------------------------------
// Display capabilities
// ---------------------------------------------------------------------------
#define HW_DISPLAY_WIDTH    320
#define HW_DISPLAY_HEIGHT   240
#define HW_DISPLAY_COLOR_DEPTH  16   // RGB565
#define HW_DISPLAY_ROTATION   1      // 0=portrait, 1=landscape, 2=portrait-inv, 3=landscape-inv
                                      // TODO: Verify landscape orientation matches physical mount

// Display interface
#define HW_DISPLAY_SPI      1        // ILI9341 via SPI
#define HW_DISPLAY_PARALLEL 0

// Display features
#define HW_DISPLAY_HAS_BACKLIGHT  1
#define HW_DISPLAY_HAS_DMA        1   // TFT_eSPI supports DMA on ESP32 — use it

// ---------------------------------------------------------------------------
// Touch capabilities
// ---------------------------------------------------------------------------
#define HW_TOUCH_PRESENT    1
#define HW_TOUCH_TYPE_RESISTIVE    1   // XPT2046
#define HW_TOUCH_TYPE_CAPACITIVE   0   // FT6236 not present on 2.8" resistive variant

// ---------------------------------------------------------------------------
// CAN / TWAI
// ---------------------------------------------------------------------------
#define HW_CAN_PRESENT      1
#define HW_CAN_CONTROLLER   TWAI      // ESP32 built-in TWAI

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
#define HW_SPIFFS_PRESENT   1         // Always available on ESP32
#define HW_SPIFFS_SIZE_KB   1024      // Depends on partition table

// TODO: Confirm if SD card slot exists on CrowPanel 2.8"
#define HW_SD_PRESENT       0         // Set 1 if SD slot confirmed

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------
#define HW_WIFI_PRESENT     1         // ESP32 has Wi-Fi (unused in Phase 1)
#define HW_BLE_PRESENT      1         // ESP32 has BLE (unused in Phase 1)

// Phase 1: all wireless disabled to save resources
#define HW_WIFI_ENABLED     0
#define HW_BLE_ENABLED      0

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

// ESP32-WROVER includes 4MB PSRAM — ESP32-WROOM does NOT
// If PSRAM is available, LVGL frame buffers can be placed there
// TODO: Determine if CrowPanel 2.8" uses WROOM or WROVER module
#define HW_PSRAM_PRESENT    0   // Conservative assumption — set 1 if confirmed
