#pragma once

#if 0

    #define LGFX_USE_V1
    #include <LovyanGFX.hpp>

    #include "board_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
     "example_alt",
     "Example Alternate Board",
    
    {
         LcdDriver::ST7789,
         23,
         -1,
         18,
         5,
         16,
         4,
         22,
         40000000UL,
         240,
         320,
         240,
         320,
         0,
         false,
         true,
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
         TouchDriver::FT6336,
         -1,
         21,
         400000UL,
         false,
    },
    
    {
         CanController::EspTwai,
         25,
         26,
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

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;

    lgfx::Light_PWM _light;

public:
    LGFX() {

    }
};

#endif
