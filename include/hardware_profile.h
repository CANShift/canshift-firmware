#pragma once
// hardware_profile.h — Board capability declarations
//
// This file describes WHAT the board can do, not HOW (pin numbers are in board_config.h).
// Use these macros to conditionally compile hardware-dependent code paths.
// Capabilities are gated by APP_* flags in app_config.h — no parallel HW_* duplicates.

// ---------------------------------------------------------------------------
// Display capabilities
// All values here are compile-time today; runtime BoardProfile (issue #17)
// will select them at boot.
// ---------------------------------------------------------------------------
#define HW_DISPLAY_WIDTH 320
#define HW_DISPLAY_HEIGHT 240
#define HW_DISPLAY_COLOR_DEPTH 16 // RGB565
// HW_DISPLAY_ROTATION values: 0=portrait, 1=landscape, 2=portrait-inv, 3=landscape-inv.
// DIS04028H + LovyanGFX: rotation 3 = USB port on the right (#40).
// Note: TFT_eSPI used rotation 1 for the same orientation — LovyanGFX has the
// opposite landscape convention.
#define HW_DISPLAY_ROTATION 3

// Native (unrotated) panel pixel dimensions — what the controller reports.
// Logical (post-rotation) dims live in HW_DISPLAY_WIDTH/HEIGHT.
#define HW_PANEL_NATIVE_WIDTH 240 // ILI9341 portrait native
#define HW_PANEL_NATIVE_HEIGHT 320

// Touch raw extents — derive from native dims minus 1.
#define HW_TOUCH_RAW_X_MAX (HW_PANEL_NATIVE_WIDTH - 1)
#define HW_TOUCH_RAW_Y_MAX (HW_PANEL_NATIVE_HEIGHT - 1)

// Per-board RAM budget (bytes) for both LVGL draw buffers combined.
// Drives LVGL_BUF_LINE_COUNT computation. 25 KB matches existing
// 320 × 20 × 2 × 2 = 25,600-byte allocation, so the existing board is byte-identical.
#define HW_LVGL_DRAW_BUDGET_BYTES (25U * 1024U)

// Display features
#define HW_DISPLAY_HAS_BACKLIGHT 1
#define HW_DISPLAY_HAS_DMA 1 // TFT_eSPI supports DMA on ESP32 — use it

// ---------------------------------------------------------------------------
// Touch capabilities
// ---------------------------------------------------------------------------
#define HW_TOUCH_PRESENT 1
#define HW_TOUCH_TYPE_RESISTIVE 1 // XPT2046

// ---------------------------------------------------------------------------
// CAN / TWAI
// ---------------------------------------------------------------------------
#define HW_CAN_PRESENT 1
#define HW_CAN_CONTROLLER TWAI // ESP32 built-in TWAI

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------
#define HW_SPIFFS_PRESENT 1    // Always available on ESP32
#define HW_SPIFFS_SIZE_KB 1024 // Depends on partition table

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------
#define HW_WIFI_PRESENT 1 // ESP32 has Wi-Fi (gated by APP_WIFI_OTA_ENABLED)
#define HW_BLE_PRESENT 1  // ESP32 has BLE (gated by APP_BLE_ENABLED)
