#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kGenericIli9341Gt911 = {
    .board_id = "generic_ili9341_gt911",
    .board_name = "Generic ESP32 + ILI9341 320x240 + GT911 capacitive",
    .chip_family = ChipFamily::Esp32,
    .lcd =
        {
            .driver = LcdDriver::ILI9341,
            .pin_mosi = 23,
            .pin_miso = 19,
            .pin_sclk = 18,
            .pin_cs = 5,
            .pin_dc = 2,
            .pin_rst = 4,
            .pin_bl = 15,
            .freq_write_hz = 27000000UL,
            .panel_width = 240,
            .panel_height = 320,
            .memory_width = 240,
            .memory_height = 320,
            .default_rotation = 3,
            .rgb_order_bgr = false,
            .invert = false,
            .bus_shared_with_touch = false,
            .readable = false,
            .color_depth = 16,
        },
    .backlight =
        {
            .present = true,
            .pwm_channel = 0,
            .pwm_freq_hz = 5000,
            .default_duty = 200,
            .invert = false,
        },
    .touch =
        {
            .driver = TouchDriver::GT911,
            .pin_cs = -1,
            .pin_irq = 27,
            .freq_hz = 400000UL,
            .needs_calibration = false,
            .pin_sda = 21,
            .pin_scl = 22,
            .pin_rst = -1,
        },
    .can =
        {
            .controller = CanController::EspTwai,
            .pin_tx = 25,
            .pin_rx = 32,
            .default_speed_kbps = 500,
        },
    .storage =
        {
            .spiffs_present = true,
            .spiffs_size_kb = 1024,
            .sd_present = false,
            .sd_pin_cs = -1,
        },
    .conn =
        {
            .wifi_supported = true,
            .ble_supported = true,
            .psram_present = false,
        },
};

}
