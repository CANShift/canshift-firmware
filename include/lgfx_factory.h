#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board.h"
#include "board_profile.h"
#include "config/board_profile_loader.h"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _spiBus;
    lgfx::Light_PWM _light;
    lgfx::Panel_ILI9341 _panelIli9341;
    lgfx::Panel_ST7789 _panelSt7789;
    lgfx::Touch_XPT2046 _touchXpt2046;
    lgfx::Touch_GT911 _touchGt911;
    lgfx::Touch_CST816S _touchCst816s;

  public:
    LGFX() {
        const canshift::boards::BoardProfile &b = canshift::boards::runtimeBoardProfile();
        configureBus(b);
        lgfx::Panel_Device *panel = configurePanel(b);
        configureBacklight(b, *panel);
        configureTouch(b, *panel);
        setPanel(panel);
    }

  private:
    static constexpr int16_t kGt911I2cAddr = 0x5D;
    static constexpr int16_t kCst816sI2cAddr = 0x15;

    void configureBus(const canshift::boards::BoardProfile &b) {
        auto cfg = _spiBus.config();
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
        _spiBus.config(cfg);
    }

    lgfx::Panel_Device *selectPanel(canshift::boards::LcdDriver driver) {
        switch (driver) {
            case canshift::boards::LcdDriver::ST7789:
                return &_panelSt7789;
            case canshift::boards::LcdDriver::ILI9341:
            default:
                return &_panelIli9341;
        }
    }

    lgfx::Panel_Device *configurePanel(const canshift::boards::BoardProfile &b) {
        lgfx::Panel_Device *panel = selectPanel(b.lcd.driver);
        panel->setBus(&_spiBus);
        auto cfg = panel->config();
        cfg.pin_cs = b.lcd.pin_cs;
        cfg.pin_rst = b.lcd.pin_rst;
        cfg.pin_busy = -1;
        cfg.memory_width = b.lcd.memory_width;
        cfg.memory_height = b.lcd.memory_height;
        cfg.panel_width = b.lcd.panel_width;
        cfg.panel_height = b.lcd.panel_height;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits = 1;
        cfg.readable = b.lcd.readable;
        cfg.invert = b.lcd.invert;
        cfg.rgb_order = b.lcd.rgb_order_bgr;
        cfg.dlen_16bit = false;
        cfg.bus_shared = b.lcd.bus_shared_with_touch;
        panel->config(cfg);
        return panel;
    }

    void configureBacklight(const canshift::boards::BoardProfile &b, lgfx::Panel_Device &panel) {
        auto cfg = _light.config();
        cfg.pin_bl = b.lcd.pin_bl;
        cfg.invert = b.backlight.invert;
        cfg.freq = b.backlight.pwm_freq_hz;
        cfg.pwm_channel = b.backlight.pwm_channel;
        _light.config(cfg);
        panel.setLight(&_light);
    }

    void configureTouch(const canshift::boards::BoardProfile &b, lgfx::Panel_Device &panel) {
        lgfx::ITouch *touch = nullptr;
        switch (b.touch.driver) {
            case canshift::boards::TouchDriver::XPT2046:
                touch = configureResistiveTouch(b);
                break;
            case canshift::boards::TouchDriver::GT911:
                touch = configureCapacitiveTouch(_touchGt911, b, kGt911I2cAddr);
                break;
            case canshift::boards::TouchDriver::CST816S:
                touch = configureCapacitiveTouch(_touchCst816s, b, kCst816sI2cAddr);
                break;
            case canshift::boards::TouchDriver::FT6336:
            case canshift::boards::TouchDriver::None:
            default:
                break;
        }
        if (touch != nullptr) {
            panel.setTouch(touch);
        }
    }

    lgfx::ITouch *configureResistiveTouch(const canshift::boards::BoardProfile &b) {
        auto cfg = _touchXpt2046.config();
        cfg.x_min = 0;
        cfg.x_max = b.lcd.panel_width - 1;
        cfg.y_min = 0;
        cfg.y_max = b.lcd.panel_height - 1;
        cfg.pin_int = b.touch.pin_irq;
        cfg.bus_shared = b.lcd.bus_shared_with_touch;
        cfg.offset_rotation = 0;
        cfg.spi_host = HSPI_HOST;
        cfg.freq = b.touch.freq_hz;
        cfg.pin_sclk = b.lcd.pin_sclk;
        cfg.pin_mosi = b.lcd.pin_mosi;
        cfg.pin_miso = b.lcd.pin_miso;
        cfg.pin_cs = b.touch.pin_cs;
        _touchXpt2046.config(cfg);
        return &_touchXpt2046;
    }

    template <typename TouchT>
    lgfx::ITouch *configureCapacitiveTouch(TouchT &touch, const canshift::boards::BoardProfile &b,
                                           int16_t i2cAddr) {
        auto cfg = touch.config();
        cfg.x_min = 0;
        cfg.x_max = b.lcd.panel_width - 1;
        cfg.y_min = 0;
        cfg.y_max = b.lcd.panel_height - 1;
        cfg.pin_int = b.touch.pin_irq;
        cfg.pin_rst = -1;
        cfg.bus_shared = false;
        cfg.offset_rotation = 0;
        cfg.i2c_port = 0;
        cfg.i2c_addr = i2cAddr;
        cfg.pin_sda = b.touch.pin_sda;
        cfg.pin_scl = b.touch.pin_scl;
        cfg.freq = b.touch.freq_hz;
        touch.config(cfg);
        return &touch;
    }
};
