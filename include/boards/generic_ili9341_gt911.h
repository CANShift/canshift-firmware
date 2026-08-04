#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    "generic_ili9341_gt911",
    "Generic ESP32 + ILI9341 320x240 + GT911 capacitive",
    ChipFamily::Esp32,

    {
        LcdDriver::ILI9341,
        23,
        19,
        18,
        5,
        2,
        4,
        15,
        27000000UL,
        240,
        320,
        240,
        320,
        3,
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
        TouchDriver::GT911,
        -1,
        27,
        400000UL,
        false,
        21,
        22,
    },

    {
        CanController::EspTwai,
        25,
        32,
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
        false,
    },
};

}
