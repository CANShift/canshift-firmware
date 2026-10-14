#include "display_driver.h"
#include "app_config.h"
#include "hardware_profile.h"
#include "config/rotation_config.h"
#include "diag/error_store.h"
#include "diag/logger.h"
#include "diag/perf_counters.h"
#include "hal/memory/psram.h"

#include <lvgl.h>

static_assert(canshift::display::kWidth > 0 && canshift::display::kHeight > 0,
              "board profile resolves to zero display dimensions");

namespace {
constexpr size_t kLvglBytesPerPixel = canshift::display::kColorDepth / 8U;
constexpr size_t kLvglNumBuffers = 2U;
constexpr size_t kLvglFloorLines = 8U;

constexpr size_t computeLvglBufLines(uint16_t screenW, size_t budgetBytes) {
    return (budgetBytes / kLvglNumBuffers / kLvglBytesPerPixel / screenW) > kLvglFloorLines
               ? (budgetBytes / kLvglNumBuffers / kLvglBytesPerPixel / screenW)
               : kLvglFloorLines;
}

void reportUnbuiltDrivers(const LGFX::ConfigureResult &result) {
    const canshift::boards::BoardProfile &board = canshift::boards::runtimeBoardProfile();
    if (!result.panelSupported) {
        LOG_ERROR("DISP",
                  "board profile asks for LCD %s, which this build cannot drive — using "
                  "ILI9341 and the panel may show nothing",
                  canshift::boards::lcdDriverName(board.lcd.driver));
        ErrorStore::push(ERROR_SRC_CONFIG, "LCD_DRV", "board profile names an unbuilt LCD driver");
    }
    if (!result.touchSupported) {
        LOG_ERROR("DISP",
                  "board profile asks for touch %s, which this build cannot drive — the "
                  "screen will not respond",
                  canshift::boards::touchDriverName(board.touch.driver));
        ErrorStore::push(ERROR_SRC_CONFIG, "TOUCH_DRV",
                         "board profile names an unbuilt touch driver");
    }
}

} // namespace

static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;

#include <Arduino.h>
#include <esp_heap_caps.h>

static LGFX s_lcd;

static lv_color_t *s_buf1 = nullptr;
static lv_color_t *s_buf2 = nullptr;
static size_t s_bufLines = 0;

void DisplayDriver::flushCallback(lv_disp_drv_t *disp, const lv_area_t *area,
                                  lv_color_t *colorMap) {
    const uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);

    s_lcd.startWrite();
    s_lcd.setAddrWindow(area->x1, area->y1, w, h);
    s_lcd.writePixels(reinterpret_cast<uint16_t *>(colorMap), w * h, true);
    s_lcd.endWrite();

#if APP_PROFILE_UI

    if (lv_disp_flush_is_last(disp)) {
        PERF_RECORD_FLUSH_FRAME();
    }
#endif

    lv_disp_flush_ready(disp);
}

namespace {

constexpr uint8_t kPanelInitAttempts = 2;
constexpr uint32_t kPanelRetryDelayMs = 50;
constexpr uint8_t kBacklightOff = 0;

void releaseDrawBuffers() {
    heap_caps_free(s_buf1);
    heap_caps_free(s_buf2);
    s_buf1 = nullptr;
    s_buf2 = nullptr;
}

bool allocateInPsram(size_t bufBytes) {
    s_buf1 = static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM));
    s_buf2 = static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM));
    if (s_buf1 && s_buf2) {
        LOG_INFO("DISP", "LVGL draw buffers allocated in PSRAM (%u bytes each)",
                 static_cast<unsigned>(bufBytes));
        return true;
    }
    releaseDrawBuffers();
    LOG_WARN("DISP", "PSRAM alloc failed — retrying in DRAM");
    return false;
}

bool allocateInDram(size_t bufBytes) {
    s_buf1 =
        static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!s_buf1) {
        LOG_ERROR("DISP", "Failed to allocate primary draw buffer (%u bytes)",
                  static_cast<unsigned>(bufBytes));
        ErrorStore::push(ERROR_SRC_SYSTEM, "DISP_BUF", "no RAM for the display draw buffer");
        return false;
    }
    s_buf2 =
        static_cast<lv_color_t *>(heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (s_buf2) {
        LOG_INFO("DISP", "LVGL draw buffers allocated in DRAM (%u bytes each)",
                 static_cast<unsigned>(bufBytes));
    } else {
        LOG_WARN("DISP", "Single draw buffer mode — heap too fragmented for second (%u bytes)",
                 static_cast<unsigned>(bufBytes));
    }
    return true;
}

bool allocateDrawBuffers() {
    const uint16_t screenW = canshift::display::width();
    s_bufLines = computeLvglBufLines(screenW, HardwareProfile::kLvglDrawBudgetBytes);
    const size_t bufBytes = static_cast<size_t>(screenW) * s_bufLines * sizeof(lv_color_t);
    if (canshift::hal::memory::isPsramAvailable() && allocateInPsram(bufBytes))
        return true;
    return allocateInDram(bufBytes);
}

bool initPanel() {
    s_lcd.setBrightness(kBacklightOff);
    for (uint8_t attempt = 1; attempt <= kPanelInitAttempts; ++attempt) {
        if (s_lcd.init())
            return true;
        LOG_WARN("DISP", "Panel init attempt %u/%u failed", static_cast<unsigned>(attempt),
                 static_cast<unsigned>(kPanelInitAttempts));
        vTaskDelay(pdMS_TO_TICKS(kPanelRetryDelayMs));
    }
    LOG_ERROR("DISP", "Panel did not initialise — the screen stays dark");
    ErrorStore::push(ERROR_SRC_SYSTEM, "DISP_INIT", "display panel did not initialise");
    return false;
}

} // namespace

bool DisplayDriver::init() {
    LOG_INFO("DISP", "Initializing LovyanGFX...");
    const bool buffersOk = allocateDrawBuffers();

    const uint8_t rotation = RotationConfig::computeLgfxRotation();
    reportUnbuiltDrivers(s_lcd.configure());
    const bool panelOk = initPanel();
    s_lcd.setRotation(rotation);
    LOG_INFO("DISP", "After setRotation(%d, offset=%u°): width=%d height=%d", rotation,
             RotationConfig::getOffsetDeg(), s_lcd.width(), s_lcd.height());

    s_lcd.fillScreen(TFT_BLACK);

    LOG_INFO("DISP", "Display initialized (%dx%d)", canshift::display::width(),
             canshift::display::height());
    return buffersOk && panelOk;
}

void DisplayDriver::backlightOn() {
    s_lcd.setBrightness(canshift::boards::runtimeBoardProfile().backlight.default_duty);
}

void DisplayDriver::registerWithLVGL() {
    LOG_INFO("DISP", "Registering display with LVGL...");

    if (!s_buf1) {
        LOG_ERROR("DISP", "Cannot register display — primary draw buffer not allocated");
        return;
    }

    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2,
                          static_cast<uint32_t>(canshift::display::width()) * s_bufLines);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res = canshift::display::width();
    s_dispDrv.ver_res = canshift::display::height();
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
