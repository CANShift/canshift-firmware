#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    "generic_esp32s3",
    "Generic ESP32-S3 + ST7789 240x320 + CST816S",
    ChipFamily::Esp32s3,

    {
        LcdDriver::ST7789,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        40000000UL,
        240,
        320,
        240,
        320,
        1,
        false,
        false,
        false,
        false,
        16,
    },

    {
        true,
        0,
        5000,
        200,
        false,
    },

    {
        TouchDriver::CST816S,
        -1,
        -1,
        400000UL,
        false,
        -1,
        -1,
    },

    {
        CanController::EspTwai,
        -1,
        -1,
        500,
    },

    {
        true,
        1024,
        false,
        -1,
    },

    {
        true,
        true,
        true,
    },
};

}
