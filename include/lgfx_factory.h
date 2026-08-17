#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board.h"
#include "touch_cst3530.h"
#include "board_profile.h"
#include "config/board_profile_loader.h"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _spiBus;
    lgfx::Light_PWM _light;
    lgfx::Panel_ILI9341 _panelIli9341;
    lgfx::Panel_ST7789 _panelSt7789;
    lgfx::Panel_ILI9488 _panelIli9488;
    lgfx::Panel_GC9A01 _panelGc9a01;
    lgfx::Touch_XPT2046 _touchXpt2046;
    lgfx::Touch_GT911 _touchGt911;
    lgfx::Touch_CST816S _touchCst816s;
    lgfx::Touch_FT5x06 _touchFt6336;
    canshift::touch::Touch_CST3530 _touchCst3530;

  public:
    struct ConfigureResult {
        bool panelSupported;
        bool touchSupported;
    };

    LGFX() = default;

    [[nodiscard]] ConfigureResult configure() {
        const canshift::boards::BoardProfile &b = canshift::boards::runtimeBoardProfile();
        configureBus(b);
        lgfx::Panel_Device *declared = selectPanel(b.lcd.driver);
        lgfx::Panel_Device *panel = declared != nullptr ? declared : &_panelIli9341;
        applyPanelConfig(*panel, b);
        configureBacklight(b, *panel);
        const bool touchSupported = configureTouch(b, *panel);
        setPanel(panel);
        return {declared != nullptr, touchSupported};
    }

  private:
    static constexpr int16_t kGt911I2cAddr = 0x5D;
    static constexpr int16_t kCst816sI2cAddr = 0x15;
    static constexpr int16_t kCst3530I2cAddr = 0x58;
    static constexpr int16_t kFt6336I2cAddr = 0x38;

    void configureBus(const canshift::boards::BoardProfile &b) {
        auto cfg = _spiBus.config();
        cfg.spi_host = SPI2_HOST;
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
            case canshift::boards::LcdDriver::ILI9341:
                return &_panelIli9341;
            case canshift::boards::LcdDriver::ST7789:
                return &_panelSt7789;
            case canshift::boards::LcdDriver::ILI9488:
                return &_panelIli9488;
            case canshift::boards::LcdDriver::GC9A01:
                return &_panelGc9a01;
        }
        return nullptr;
    }

    void applyPanelConfig(lgfx::Panel_Device &panel, const canshift::boards::BoardProfile &b) {
        panel.setBus(&_spiBus);
        auto cfg = panel.config();
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
        panel.config(cfg);
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

    lgfx::ITouch *selectTouch(const canshift::boards::BoardProfile &b) {
        switch (b.touch.driver) {
            case canshift::boards::TouchDriver::XPT2046:
                return configureResistiveTouch(b);
            case canshift::boards::TouchDriver::GT911:
                return configureCapacitiveTouch(_touchGt911, b, kGt911I2cAddr);
            case canshift::boards::TouchDriver::CST816S:
                return configureCapacitiveTouch(_touchCst816s, b, kCst816sI2cAddr);
            case canshift::boards::TouchDriver::CST3530:
                return configureCapacitiveTouch(_touchCst3530, b, kCst3530I2cAddr);
            case canshift::boards::TouchDriver::FT6336:
                return configureCapacitiveTouch(_touchFt6336, b, kFt6336I2cAddr);
            case canshift::boards::TouchDriver::None:
                break;
        }
        return nullptr;
    }

    bool configureTouch(const canshift::boards::BoardProfile &b, lgfx::Panel_Device &panel) {
        if (b.touch.driver == canshift::boards::TouchDriver::None)
            return true;
        lgfx::ITouch *touch = selectTouch(b);
        if (touch == nullptr)
            return false;
        panel.setTouch(touch);
        return true;
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
        cfg.spi_host = SPI2_HOST;
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
        cfg.pin_rst = b.touch.pin_rst;
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
