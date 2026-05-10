// display_driver.cpp — ILI9341 display HAL (LovyanGFX backend)
// In sim mode: minimal LVGL stub with tiny draw buffers, no LovyanGFX.
// In hardware mode: LGFX (custom panel class) with double-buffered DMA flush.

#include "display_driver.h"
#include "app_config.h"
#include "hardware_profile.h"
#include "config/rotation_config.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"
#include "hal/memory/psram.h"

#include <lvgl.h>

static_assert(HW_DISPLAY_WIDTH == 320 && HW_DISPLAY_HEIGHT == 240,
              "expected CrowPanel 2.8\" 320×240 — override BoardProfile if changing");

// Derive the LVGL draw-buffer line count from the per-board RAM budget so a
// new BoardProfile only has to set HW_LVGL_DRAW_BUDGET_BYTES. Floor at 8
// lines so absurdly tight budgets still yield a usable buffer.
namespace {
constexpr size_t kLvglBytesPerPixel = HW_DISPLAY_COLOR_DEPTH / 8U;
constexpr size_t kLvglNumBuffers = 2U;
constexpr size_t kLvglFloorLines = 8U;

constexpr size_t computeLvglBufLines(uint16_t screenW, size_t budgetBytes) {
    return (budgetBytes / kLvglNumBuffers / kLvglBytesPerPixel / screenW) > kLvglFloorLines
               ? (budgetBytes / kLvglNumBuffers / kLvglBytesPerPixel / screenW)
               : kLvglFloorLines;
}

constexpr size_t kLvglBufLines = computeLvglBufLines(HW_DISPLAY_WIDTH, HW_LVGL_DRAW_BUDGET_BYTES);
static_assert(kLvglBufLines == 20, "draw-buffer line count regression for CrowPanel 2.8\"");
} // namespace

static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;

#if !APP_SIMULATION_MODE

    #include "board_config.h"
    #include <Arduino.h>
    #include <esp_heap_caps.h>

static LGFX s_lcd;

static lv_color_t *s_buf1 = nullptr;
static lv_color_t *s_buf2 = nullptr;

// Invoked from lv_task_handler() — the UI-task caller already holds g_lvglMutex.
void DisplayDriver::flushCallback(lv_disp_drv_t *disp, const lv_area_t *area,
                                  lv_color_t *colorMap) {
    const uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

    s_lcd.startWrite();
    s_lcd.setAddrWindow(area->x1, area->y1, w, h);
    s_lcd.writePixels(reinterpret_cast<uint16_t *>(colorMap), w * h, true /* swap */);
    s_lcd.endWrite();

    #if APP_PROFILE_UI
    // Count completed frames (last region of the refresh) only — partial
    // flush regions inflate the FPS metric otherwise.
    if (lv_disp_flush_is_last(disp)) {
        PERF_RECORD_FLUSH_FRAME();
    }
    #endif

    lv_disp_flush_ready(disp);
}

void DisplayDriver::init() {
    LOG_INFO("DISP", "Initializing LovyanGFX...");

    const size_t bufBytes = HW_DISPLAY_WIDTH * kLvglBufLines * sizeof(lv_color_t);

    // Offload LVGL draw buffers to PSRAM when the chip is a WROVER variant
    // (issue #563). LovyanGFX handles PSRAM-resident pixel data via its DMA
    // bus path. On a WROOM (no PSRAM), fall back to DMA-capable internal RAM
    // — byte-identical to the pre-#563 behaviour.
    const bool offloadToPsram = canshift::hal::memory::isPsramAvailable();
    if (offloadToPsram) {
        s_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM));
        s_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM));
        if (s_buf1 && s_buf2) {
            LOG_INFO("DISP", "LVGL draw buffers allocated in PSRAM (%u bytes each)",
                     static_cast<unsigned>(bufBytes));
        } else {
            // Free whatever did succeed and fall through to the DRAM path so
            // the device still boots if PSRAM ran out unexpectedly.
            if (s_buf1) {
                heap_caps_free(s_buf1);
                s_buf1 = nullptr;
            }
            if (s_buf2) {
                heap_caps_free(s_buf2);
                s_buf2 = nullptr;
            }
            LOG_WARN("DISP", "PSRAM alloc failed — retrying in DRAM");
        }
    }

    if (!s_buf1 || !s_buf2) {
        s_buf1 = static_cast<lv_color_t *>(
            heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        s_buf2 = static_cast<lv_color_t *>(
            heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (!s_buf1 || !s_buf2) {
            LOG_ERROR("DISP", "Failed to allocate LVGL draw buffers (%u bytes each)",
                      static_cast<unsigned>(bufBytes));
            return;
        }
        LOG_INFO("DISP", "LVGL draw buffers allocated in DRAM (%u bytes each)",
                 static_cast<unsigned>(bufBytes));
    }

    const uint8_t rotation = RotationConfig::computeLgfxRotation();
    s_lcd.init();
    s_lcd.setRotation(rotation);
    s_lcd.setBrightness(BL_DEFAULT_DUTY);
    LOG_INFO("DISP", "After setRotation(%d, offset=%u°): width=%d height=%d", rotation,
             RotationConfig::getOffsetDeg(), s_lcd.width(), s_lcd.height());

    s_lcd.fillScreen(TFT_BLACK);

    LOG_INFO("DISP", "Display initialized (%dx%d)", HW_DISPLAY_WIDTH, HW_DISPLAY_HEIGHT);
}

void DisplayDriver::registerWithLVGL() {
    LOG_INFO("DISP", "Registering display with LVGL...");

    if (!s_buf1 || !s_buf2) {
        LOG_ERROR("DISP", "Cannot register display — draw buffers not allocated");
        return;
    }

    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, HW_DISPLAY_WIDTH * kLvglBufLines);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res = HW_DISPLAY_WIDTH;
    s_dispDrv.ver_res = HW_DISPLAY_HEIGHT;
    s_dispDrv.flush_cb = flushCallback;
    s_dispDrv.draw_buf = &s_drawBuf;
    lv_disp_drv_register(&s_dispDrv);

    LOG_INFO("DISP", "LVGL display driver registered");
}

void DisplayDriver::setBacklight(uint8_t brightness) {
    s_lcd.setBrightness(brightness);
}

LGFX &DisplayDriver::getDisplay() {
    return s_lcd;
}

#else // APP_SIMULATION_MODE

static constexpr uint16_t SIM_BUF_LINES = 4;
static lv_color_t s_buf1[HW_DISPLAY_WIDTH * SIM_BUF_LINES];
static lv_color_t s_buf2[HW_DISPLAY_WIDTH * SIM_BUF_LINES];

// Invoked from lv_task_handler() — the UI-task caller already holds g_lvglMutex.
void DisplayDriver::flushCallback(lv_disp_drv_t *disp, const lv_area_t * /*area*/,
                                  lv_color_t * /*colorMap*/) {
    #if APP_PROFILE_UI
    if (lv_disp_flush_is_last(disp)) {
        PERF_RECORD_FLUSH_FRAME();
    }
    #endif
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
