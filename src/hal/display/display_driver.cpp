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
    #include <esp_heap_caps.h>

static TFT_eSPI s_tft;

// Draw buffer pointers — allocated from DMA-capable heap in init() so they
// do not occupy BSS (51 KB static would overflow dram0_0_seg).
static lv_color_t *s_buf1 = nullptr;
static lv_color_t *s_buf2 = nullptr;

void DisplayDriver::flushCallback(lv_disp_drv_t *disp, const lv_area_t *area,
                                  lv_color_t *colorMap) {
    const uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

    s_tft.startWrite();
    s_tft.setAddrWindow(area->x1, area->y1, w, h);
    // pushColors with byte-swap mirrors Elecrow's reference flush — required for
    // the panel byte order on this board even though LV_COLOR_16_SWAP=1.
    s_tft.pushColors(reinterpret_cast<uint16_t *>(colorMap), w * h, true);
    s_tft.endWrite();

    lv_disp_flush_ready(disp);
}

void DisplayDriver::init() {
    LOG_INFO("DISP", "Initializing TFT_eSPI...");

    // Allocate draw buffers from the DMA-capable internal heap.
    // This keeps them out of the static BSS segment (which is severely limited).
    const size_t bufBytes = HW_DISPLAY_WIDTH * LVGL_BUF_LINE_COUNT * sizeof(lv_color_t);
    s_buf1 =
        static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    s_buf2 =
        static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!s_buf1 || !s_buf2) {
        LOG_ERROR("DISP", "Failed to allocate LVGL draw buffers (%u bytes each)", bufBytes);
        return;
    }

    s_tft.init();
    s_tft.setRotation(HW_DISPLAY_ROTATION);
    LOG_INFO("DISP", "After setRotation(%d): tft.width=%d tft.height=%d",
             HW_DISPLAY_ROTATION, s_tft.width(), s_tft.height());

    // Diagnostic readback (issue #40):
    // 0xD3 (RDID4) on ILI9341 returns 0x00, 0x93, 0x41 at indices 1/2/3.
    // 0x04 (RDDID) returns manufacturer / version / driver id.
    // Garbage / 0x00 / 0xFF on all → SPI not reaching controller (pinout/cs/miso).
    // Different signature → controller is not ILI9341 (e.g. ST7789).
    const uint8_t id1 = s_tft.readcommand8(0xD3, 1);
    const uint8_t id2 = s_tft.readcommand8(0xD3, 2);
    const uint8_t id3 = s_tft.readcommand8(0xD3, 3);
    const uint8_t rd_mfg = s_tft.readcommand8(0x04, 1);
    const uint8_t rd_ver = s_tft.readcommand8(0x04, 2);
    const uint8_t rd_drv = s_tft.readcommand8(0x04, 3);
    LOG_INFO("DISP", "RDID4 (0xD3): %02X %02X %02X (expect 00 93 41 for ILI9341)", id1, id2, id3);
    LOG_INFO("DISP", "RDDID (0x04): mfg=%02X ver=%02X drv=%02X", rd_mfg, rd_ver, rd_drv);

    // Backlight ON before any pixel push so the panel is observable
    // and not still in reset when fillScreen runs.
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ_HZ, BL_PWM_BITS);
    ledcAttachPin(PIN_TFT_BL, BL_PWM_CHANNEL);
    setBacklight(BL_DEFAULT_DUTY);

    // Diagnostic: RGB cycle to localize white-screen fault (issue #40).
    // - Cycles R→G→B→black → TFT_eSPI init OK, fault is downstream (LVGL flush).
    // - Stays white → SPI / panel init is broken (pinout, RST, clock).
    LOG_INFO("DISP", "RGB diagnostic — RED");
    s_tft.fillScreen(TFT_RED);
    delay(400);
    LOG_INFO("DISP", "RGB diagnostic — GREEN");
    s_tft.fillScreen(TFT_GREEN);
    delay(400);
    LOG_INFO("DISP", "RGB diagnostic — BLUE");
    s_tft.fillScreen(TFT_BLUE);
    delay(400);
    s_tft.fillScreen(TFT_BLACK);

    LOG_INFO("DISP", "Display initialized (%dx%d)", HW_DISPLAY_WIDTH, HW_DISPLAY_HEIGHT);
}

void DisplayDriver::registerWithLVGL() {
    LOG_INFO("DISP", "Registering display with LVGL...");

    if (!s_buf1 || !s_buf2) {
        LOG_ERROR("DISP", "Cannot register display — draw buffers not allocated");
        return;
    }

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

TFT_eSPI &DisplayDriver::getTft() {
    return s_tft;
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
