#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board.h"
#include "hardware_profile.h"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel;
    lgfx::Bus_SPI _bus;
    lgfx::Touch_XPT2046 _touch;
    lgfx::Light_PWM _light;

  public:
    LGFX() {
        const auto &b = canshift::boards::kActiveBoard;
        {
            auto cfg = _bus.config();
            cfg.spi_host = HSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = b.lcd.freq_write_hz;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = 1;
            cfg.pin_sclk = b.lcd.pin_sclk;
            cfg.pin_mosi = b.lcd.pin_mosi;
            cfg.pin_miso = b.lcd.pin_miso;
            cfg.pin_dc = b.lcd.pin_dc;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg = _panel.config();
            cfg.pin_cs = b.lcd.pin_cs;
            cfg.pin_rst = b.lcd.pin_rst;
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

            cfg.readable = false;
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl = b.lcd.pin_bl;
            cfg.invert = b.backlight.invert;
            cfg.freq = b.backlight.pwm_freq_hz;
            cfg.pwm_channel = b.backlight.pwm_channel;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.x_min = 0;
            cfg.x_max = HW_TOUCH_RAW_X_MAX;
            cfg.y_min = 0;
            cfg.y_max = HW_TOUCH_RAW_Y_MAX;
            cfg.pin_int = b.touch.pin_irq;
            cfg.bus_shared = true;
            cfg.offset_rotation = 0;
            cfg.spi_host = HSPI_HOST;
            cfg.freq = b.touch.freq_hz;
            cfg.pin_sclk = b.lcd.pin_sclk;
            cfg.pin_mosi = b.lcd.pin_mosi;
            cfg.pin_miso = b.lcd.pin_miso;
            cfg.pin_cs = b.touch.pin_cs;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
