#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    "generic_ili9341",
    "Generic ESP32 + ILI9341 320x240 + XPT2046",
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
        true,
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
        TouchDriver::XPT2046,
        21,
        27,
        2500000UL,
        true,
        -1,
        -1,
        -1,
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
