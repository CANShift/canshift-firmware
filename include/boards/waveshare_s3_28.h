#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    "waveshare_s3_28",
    "Waveshare ESP32-S3-Touch-LCD-2.8 (ST7789T3 + CST3530)",
    ChipFamily::Esp32s3,

    {
        LcdDriver::ST7789,
        45,
        -1,
        40,
        42,
        41,
        39,
        5,
        80000000UL,
        240,
        320,
        240,
        320,
        1,
        false,
        true,
        false,
        false,
        16,
    },

    {
        true,
        1,
        20000,
        200,
        false,
    },

    {
        TouchDriver::CST3530,
        -1,
        -1,
        400000UL,
        false,
        1,
        3,
        2,
    },

    {
        CanController::EspTwai,
        17,
        18,
        500,
    },

    {
        true,
        1024,
        true,
        -1,
    },

    {
        true,
        true,
        true,
    },
};

} // namespace canshift::boards
