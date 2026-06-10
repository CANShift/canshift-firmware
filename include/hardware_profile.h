#pragma once
// Board capabilities (the HOW lives in board_config.h).

#define HW_DISPLAY_WIDTH 320
#define HW_DISPLAY_HEIGHT 240
#define HW_DISPLAY_COLOR_DEPTH 16
// LovyanGFX rotation 3 = USB port on the right (#40). Opposite to TFT_eSPI.
#define HW_DISPLAY_ROTATION 3

#define HW_PANEL_NATIVE_WIDTH 240
#define HW_PANEL_NATIVE_HEIGHT 320

#define HW_TOUCH_RAW_X_MAX (HW_PANEL_NATIVE_WIDTH - 1)
#define HW_TOUCH_RAW_Y_MAX (HW_PANEL_NATIVE_HEIGHT - 1)

// 12.8 KB → two 6.4 KB buffers. 25 KB failed second-buffer alloc on the
// crowpanel_28_wifi env post-lv_init — single-buffer drops 50 → ~5 fps.
#define HW_LVGL_DRAW_BUDGET_BYTES (12U * 1024U + 800U)

#define HW_DISPLAY_HAS_BACKLIGHT 1
#define HW_DISPLAY_HAS_DMA 1

#define HW_TOUCH_PRESENT 1
#define HW_TOUCH_TYPE_RESISTIVE 1

#define HW_CAN_PRESENT 1
#define HW_CAN_CONTROLLER TWAI

#define HW_SPIFFS_PRESENT 1
#define HW_SPIFFS_SIZE_KB 1024

#define HW_WIFI_PRESENT 1
#define HW_BLE_PRESENT 1
