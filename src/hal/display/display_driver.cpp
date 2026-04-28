// display_driver.cpp — ILI9341 display HAL implementation

#include "display_driver.h"
#include "board_config.h"
#include "app_config.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <TFT_eSPI.h>
#include <lvgl.h>
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

static TFT_eSPI s_tft;

// LVGL draw buffers — two partial buffers for double buffering
// Buffer size: width × LVGL_BUF_LINE_COUNT × 2 bytes (RGB565)
static lv_color_t s_buf1[HW_DISPLAY_WIDTH * LVGL_BUF_LINE_COUNT];
static lv_color_t s_buf2[HW_DISPLAY_WIDTH * LVGL_BUF_LINE_COUNT];

static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t      s_dispDrv;

// ---------------------------------------------------------------------------
// LVGL flush callback
// ---------------------------------------------------------------------------

void DisplayDriver::flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorMap) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);

    // pushPixels expects 16-bit values. lv_color_t is RGB565 in our config.
    s_tft.pushPixels(reinterpret_cast<uint16_t*>(colorMap), w * h);

    s_tft.endWrite();

    // Notify LVGL that flush is complete
    lv_disp_flush_ready(disp);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void DisplayDriver::init() {
    LOG_INFO("DISP", "Initializing TFT_eSPI...");

    // TFT_eSPI is configured via build flags (USER_SETUP_LOADED=1).
    // The library reads pin definitions from our board_config.h values
    // passed through build_flags in platformio.ini.
    // TODO: Create include/User_Setup.h with all TFT_eSPI pin definitions
    //       and verify it matches board_config.h.

    s_tft.init();
    s_tft.setRotation(HW_DISPLAY_ROTATION);
    s_tft.fillScreen(TFT_BLACK);

    // Configure backlight PWM
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ_HZ, BL_PWM_BITS);
    ledcAttachPin(PIN_TFT_BL, BL_PWM_CHANNEL);
    setBacklight(BL_DEFAULT_DUTY);

    LOG_INFO("DISP", "Display initialized (%dx%d)", HW_DISPLAY_WIDTH, HW_DISPLAY_HEIGHT);
}

void DisplayDriver::registerWithLVGL() {
    LOG_INFO("DISP", "Registering display with LVGL...");

    // Initialize draw buffer with two partial buffers
    lv_disp_draw_buf_init(
        &s_drawBuf,
        s_buf1,
        s_buf2,
        HW_DISPLAY_WIDTH * LVGL_BUF_LINE_COUNT
    );

    // Register display driver
    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res   = HW_DISPLAY_WIDTH;
    s_dispDrv.ver_res   = HW_DISPLAY_HEIGHT;
    s_dispDrv.flush_cb  = flushCallback;
    s_dispDrv.draw_buf  = &s_drawBuf;

    // Enable full dirty refresh for simplicity (may change to partial for performance)
    // s_dispDrv.full_refresh = 1;

    lv_disp_drv_register(&s_dispDrv);

    LOG_INFO("DISP", "LVGL display driver registered");
}

void DisplayDriver::setBacklight(uint8_t brightness) {
    ledcWrite(BL_PWM_CHANNEL, brightness);
}
