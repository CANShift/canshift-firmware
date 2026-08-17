#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    .board_id = "generic_esp32s3",
    .board_name = "Generic ESP32-S3 + ST7789 240x320 + CST816S",
    .chip_family = ChipFamily::Esp32s3,
    .lcd =
        {
            .driver = LcdDriver::ST7789,
            .pin_mosi = -1,
            .pin_miso = -1,
            .pin_sclk = -1,
            .pin_cs = -1,
            .pin_dc = -1,
            .pin_rst = -1,
            .pin_bl = -1,
            .freq_write_hz = 40000000UL,
            .panel_width = 240,
            .panel_height = 320,
            .memory_width = 240,
            .memory_height = 320,
            .default_rotation = 1,
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
            .driver = TouchDriver::CST816S,
            .pin_cs = -1,
            .pin_irq = -1,
            .freq_hz = 400000UL,
            .needs_calibration = false,
            .pin_sda = -1,
            .pin_scl = -1,
            .pin_rst = -1,
        },
    .can =
        {
            .controller = CanController::EspTwai,
            .pin_tx = -1,
            .pin_rx = -1,
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
            .psram_present = true,
        },
};

}
