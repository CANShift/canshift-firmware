#pragma once

#include <stdint.h>

namespace canshift::boards {

enum class ChipFamily : uint8_t { Esp32, Esp32s3 };
enum class LcdDriver : uint8_t { ILI9341, ST7789, ILI9488, GC9A01 };
enum class TouchDriver : uint8_t { None, XPT2046, FT6336, GT911, CST816S, CST3530 };
enum class CanController : uint8_t { None, EspTwai };

struct LcdProfile {
    LcdDriver driver;
    int8_t pin_mosi;
    int8_t pin_miso;
    int8_t pin_sclk;
    int8_t pin_cs;
    int8_t pin_dc;
    int8_t pin_rst;
    int8_t pin_bl;
    uint32_t freq_write_hz;
    uint16_t panel_width;
    uint16_t panel_height;
    uint16_t memory_width;
    uint16_t memory_height;
    uint8_t default_rotation;
    bool rgb_order_bgr;
    bool invert;
    bool bus_shared_with_touch;
    bool readable;
    uint8_t color_depth;
};

struct BacklightProfile {
    bool present;
    uint8_t pwm_channel;
    uint32_t pwm_freq_hz;
    uint8_t default_duty;
    bool invert;
};

struct TouchProfile {
    TouchDriver driver;
    int8_t pin_cs;
    int8_t pin_irq;
    uint32_t freq_hz;
    bool needs_calibration;
    int8_t pin_sda;
    int8_t pin_scl;
    int8_t pin_rst;
};

struct CanProfile {
    CanController controller;
    int8_t pin_tx;
    int8_t pin_rx;
    uint16_t default_speed_kbps;
};

struct StorageProfile {
    bool spiffs_present;
    uint16_t spiffs_size_kb;
    bool sd_present;
    int8_t sd_pin_cs;
};

struct ConnectivityProfile {
    bool wifi_supported;
    bool ble_supported;
    bool psram_present;
};

struct BoardProfile {
    const char *board_id;
    const char *board_name;
    ChipFamily chip_family;
    LcdProfile lcd;
    BacklightProfile backlight;
    TouchProfile touch;
    CanProfile can;
    StorageProfile storage;
    ConnectivityProfile conn;
};

constexpr bool lcdRotationSwapsAxes(uint8_t rotation) {
    return (rotation & 1u) != 0u;
}

constexpr uint16_t lcdLogicalWidth(const LcdProfile &lcd) {
    return lcdRotationSwapsAxes(lcd.default_rotation) ? lcd.panel_height : lcd.panel_width;
}

constexpr uint16_t lcdLogicalHeight(const LcdProfile &lcd) {
    return lcdRotationSwapsAxes(lcd.default_rotation) ? lcd.panel_width : lcd.panel_height;
}

} // namespace canshift::boards
