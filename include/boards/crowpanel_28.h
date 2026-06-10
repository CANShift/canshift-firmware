#pragma once
// Profile (kActiveBoard) is self-contained — does NOT reference board_config.h
// macros — so a future board can drop in without circular includes.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_config.h"
#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    /* board_id   */ "crowpanel_28",
    /* board_name */ "Elecrow CrowPanel 2.8\" ESP32",
    /* lcd */
    {
        /* driver                */ LcdDriver::ILI9341,
        /* pin_mosi              */ 13,
        /* pin_miso              */ 12,
        /* pin_sclk              */ 14,
        /* pin_cs                */ 15,
        /* pin_dc                */ 2,
        /* pin_rst               */ -1,
        /* pin_bl                */ 27,
        /* freq_write_hz         */ 27000000UL,
        /* panel_width           */ 240,
        /* panel_height          */ 320,
        /* memory_width          */ 240,
        /* memory_height         */ 320,
        /* default_rotation      */ 3,
        /* rgb_order_bgr         */ false,
        /* invert                */ false,
        /* bus_shared_with_touch */ true,
        /* readable              */ false,
        /* color_depth           */ 16,
    },
    /* backlight */
    {
        /* present       */ true,
        /* pwm_channel   */ 0,
        /* pwm_freq_hz   */ 5000,
        /* default_duty  */ 200,
        /* invert        */ false,
    },
    /* touch */
    {
        /* driver             */ TouchDriver::XPT2046,
        /* pin_cs             */ 33,
        /* pin_irq            */ -1,
        /* freq_hz            */ 2500000UL,
        /* needs_calibration  */ true,
    },
    /* can */
    {
        /* controller         */ CanController::EspTwai,
        /* pin_tx             */ 25,
        /* pin_rx             */ 32,
        /* default_speed_kbps */ 500,
    },
    /* storage */
    {
        /* spiffs_present  */ true,
        /* spiffs_size_kb  */ 1024,
        /* sd_present      */ false,
        /* sd_pin_cs       */ -1,
    },
    /* conn */
    {
        /* wifi_supported */ true,
        /* ble_supported  */ true,
        /* psram_present  */ false,
    },
};

} // namespace canshift::boards

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Touch_XPT2046 _touch;
    lgfx::Light_PWM _light;

  public:
    LGFX() {
        {
            auto cfg = _bus.config();
            cfg.spi_host = HSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = TFT_SPI_FREQ_HZ;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = 1;
            cfg.pin_sclk = PIN_TFT_SCLK;
            cfg.pin_mosi = PIN_TFT_MOSI;
            cfg.pin_miso = PIN_TFT_MISO;
            cfg.pin_dc = PIN_TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = PIN_TFT_CS;
            cfg.pin_rst = PIN_TFT_RST;
            cfg.pin_busy = -1;
            cfg.memory_width = HW_PANEL_NATIVE_WIDTH;
            cfg.memory_height = HW_PANEL_NATIVE_HEIGHT;
            cfg.panel_width = HW_PANEL_NATIVE_WIDTH;
            cfg.panel_height = HW_PANEL_NATIVE_HEIGHT;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            // MISO not wired; touch shares the SPI bus.
            cfg.readable = false;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = PIN_TFT_BL;
            cfg.invert = false;
            cfg.freq = BL_PWM_FREQ_HZ;
            cfg.pwm_channel = BL_PWM_CHANNEL;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = HW_TOUCH_RAW_X_MAX;
            cfg.y_min = 0;
            cfg.y_max = HW_TOUCH_RAW_Y_MAX;
            cfg.pin_int = PIN_TOUCH_IRQ;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host = HSPI_HOST;
            cfg.freq = TOUCH_SPI_FREQ_HZ;
            cfg.pin_sclk = PIN_TFT_SCLK;
            cfg.pin_mosi = PIN_TFT_MOSI;
            cfg.pin_miso = PIN_TFT_MISO;
            cfg.pin_cs = PIN_TOUCH_CS;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
