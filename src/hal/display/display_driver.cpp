// display_driver.cpp — ILI9341 display HAL
// In sim mode: minimal LVGL stub with tiny draw buffers, no TFT_eSPI.
// In hardware mode: full TFT_eSPI driver with double-buffered DMA flush.

#include "display_driver.h"
#include "app_config.h"
#include "hardware_profile.h"
#include "diag/logger.h"

#include <lvgl.h>

// ---------------------------------------------------------------------------
// Shared state (both modes need a registered LVGL display)
// ---------------------------------------------------------------------------

static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;

// ---------------------------------------------------------------------------
// HARDWARE MODE
// ---------------------------------------------------------------------------

#if !APP_SIMULATION_MODE

    #include "board_config.h"
    #include <TFT_eSPI.h>
    #include <Arduino.h>

static TFT_eSPI s_tft;

// Two partial draw buffers — 40 lines each, double-buffered
static lv_color_t s_buf1[HW_DISPLAY_WIDTH * LVGL_BUF_LINE_COUNT];
static lv_color_t s_buf2[HW_DISPLAY_WIDTH * LVGL_BUF_LINE_COUNT];

void DisplayDriver::flushCallback(lv_disp_drv_t *disp, const lv_area_t *area,
                                  lv_color_t *colorMap) {
    const uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    s_tft.pushPixels(reinterpret_cast<uint16_t *>(colorMap), w * h);
    s_tft.endWrite();

    lv_disp_flush_ready(disp);
}

void DisplayDriver::init() {
    LOG_INFO("DISP", "Initializing TFT_eSPI...");

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

    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, HW_DISPLAY_WIDTH * LVGL_BUF_LINE_COUNT);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res = HW_DISPLAY_WIDTH;
    s_dispDrv.ver_res = HW_DISPLAY_HEIGHT;
    s_dispDrv.flush_cb = flushCallback;
    s_dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&s_dispDrv);

    LOG_INFO("DISP", "LVGL display driver registered");
}

void DisplayDriver::setBacklight(uint8_t brightness) {
    ledcWrite(BL_PWM_CHANNEL, brightness);
}

// ---------------------------------------------------------------------------
// SIMULATION MODE — no TFT_eSPI, minimal draw buffers, discards all output
// ---------------------------------------------------------------------------

#else // APP_SIMULATION_MODE

// 4-line buffers keep .bss small; LVGL flushes and immediately continues.
static constexpr uint16_t SIM_BUF_LINES = 4;
static lv_color_t s_buf1[HW_DISPLAY_WIDTH * SIM_BUF_LINES];
static lv_color_t s_buf2[HW_DISPLAY_WIDTH * SIM_BUF_LINES];

void DisplayDriver::flushCallback(lv_disp_drv_t *disp, const lv_area_t * /*area*/,
                                  lv_color_t * /*colorMap*/) {
    lv_disp_flush_ready(disp);
}

void DisplayDriver::init() {
    LOG_INFO("DISP", "Sim mode — display stub active (no hardware)");
}

void DisplayDriver::registerWithLVGL() {
    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, HW_DISPLAY_WIDTH * SIM_BUF_LINES);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res = HW_DISPLAY_WIDTH;
    s_dispDrv.ver_res = HW_DISPLAY_HEIGHT;
    s_dispDrv.flush_cb = flushCallback;
    s_dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&s_dispDrv);
}

void DisplayDriver::setBacklight(uint8_t /*brightness*/) {}

#endif // APP_SIMULATION_MODE
