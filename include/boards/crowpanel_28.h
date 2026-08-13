#pragma once

#include "board_profile.h"
#include "hardware_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    "crowpanel_28",
    "Elecrow CrowPanel 2.8\" ESP32",
    ChipFamily::Esp32,

    {
        LcdDriver::ILI9341,
        13,
        12,
        14,
        15,
        2,
        -1,
        27,
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
        33,
        -1,
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
