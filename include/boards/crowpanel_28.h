#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kCrowpanel28 = {
    .board_id = "crowpanel_28",
    .board_name = "Elecrow CrowPanel 2.8\" ESP32",
    .chip_family = ChipFamily::Esp32,
    .lcd =
        {
            .driver = LcdDriver::ILI9341,
            .pin_mosi = 13,
            .pin_miso = 12,
            .pin_sclk = 14,
            .pin_cs = 15,
            .pin_dc = 2,
            .pin_rst = -1,
            .pin_bl = 27,
            .freq_write_hz = 27000000UL,
            .panel_width = 240,
            .panel_height = 320,
            .memory_width = 240,
            .memory_height = 320,
            .default_rotation = 3,
            .rgb_order_bgr = false,
            .invert = false,
            .bus_shared_with_touch = true,
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
            .driver = TouchDriver::XPT2046,
            .pin_cs = 33,
            .pin_irq = -1,
            .freq_hz = 2500000UL,
            .needs_calibration = true,
            .pin_sda = -1,
            .pin_scl = -1,
            .pin_rst = -1,
        },
    .can =
        {
            .controller = CanController::EspTwai,
            .pin_tx = 22,
            .pin_rx = 21,
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
