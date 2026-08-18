#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kWaveshareS328 = {
    .board_id = "waveshare_s3_28",
    .board_name = "Waveshare ESP32-S3-Touch-LCD-2.8 (ST7789T3 + CST3530)",
    .chip_family = ChipFamily::Esp32s3,
    .lcd =
        {
            .driver = LcdDriver::ST7789,
            .pin_mosi = 45,
            .pin_miso = -1,
            .pin_sclk = 40,
            .pin_cs = 42,
            .pin_dc = 41,
            .pin_rst = 39,
            .pin_bl = 5,
            .freq_write_hz = 80000000UL,
            .panel_width = 240,
            .panel_height = 320,
            .memory_width = 240,
            .memory_height = 320,
            .default_rotation = 1,
            .rgb_order_bgr = false,
            .invert = true,
            .bus_shared_with_touch = false,
            .readable = false,
            .color_depth = 16,
        },
    .backlight =
        {
            .present = true,
            .pwm_channel = 1,
            .pwm_freq_hz = 20000,
            .default_duty = 200,
            .invert = false,
        },
    .touch =
        {
            .driver = TouchDriver::CST3530,
            .pin_cs = -1,
            .pin_irq = -1,
            .freq_hz = 400000UL,
            .needs_calibration = false,
            .pin_sda = 1,
            .pin_scl = 3,
            .pin_rst = 2,
        },
    .can =
        {
            .controller = CanController::EspTwai,
            .pin_tx = 17,
            .pin_rx = 18,
            .default_speed_kbps = 500,
        },
    .storage =
        {
            .spiffs_present = true,
            .spiffs_size_kb = 1024,
            .sd_present = true,
            .sd_pin_cs = -1,
        },
    .conn =
        {
            .wifi_supported = true,
            .ble_supported = true,
            .psram_present = true,
        },
};

} // namespace canshift::boards
