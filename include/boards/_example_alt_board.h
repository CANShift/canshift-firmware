#pragma once
// boards/_example_alt_board.h — Reference template for adding a new board.
//
// THIS FILE IS NOT COMPILED. It is a checklist + commented-out skeleton you
// copy when porting CANShift to a new HMI panel. Every block below is
// commented out so the file can sit in the include path harmlessly.
//
// -----------------------------------------------------------------------
// New-board onboarding checklist
// -----------------------------------------------------------------------
//   1. Copy this file to include/boards/<your_board>.h.
//   2. Define `constexpr canshift::boards::BoardProfile kActiveBoard = {...}`
//      with literal pin / size / driver values from the board's datasheet.
//      Keep it self-contained — do NOT reference macros from board_config.h
//      so future boards stay decoupled from the legacy compat shim.
//   3. Pick the LovyanGFX driver classes that match the panel + touch:
//        Panel_ILI9341 / Panel_ST7789 / Panel_ILI9488 / Panel_GC9A01
//        Touch_XPT2046 / Touch_FT5x06 / Touch_GT911 / Touch_CST816S
//      and write the LGFX class (mirror the structure in crowpanel_28.h).
//   4. Add an `#elif defined(BOARD_<YOUR_BOARD>)` clause to include/board.h
//      that includes your new header.
//   5. Add an [env:<your_board>] section to platformio.ini that:
//        - extends [base_flags]
//        - sets `-DBOARD_<YOUR_BOARD>=1` in build_flags
//        - selects an appropriate partition table + filesystem
//   6. Run `pio run -e <your_board>` and `pio run -e crowpanel_28` to
//      confirm both boards still build clean.
//   7. Verify on real hardware: TFT init, touch calibration, CAN RX, SPIFFS
//      mount, backlight PWM. Do NOT ship until all five are exercised.
//   8. Open a tracking issue with the board model number, schematic source,
//      and hardware verification status (which steps from #7 are done).
// -----------------------------------------------------------------------

#if 0 // -- commented-out reference template (do not compile) ---------------

    #define LGFX_USE_V1
    #include <LovyanGFX.hpp>

    #include "board_profile.h"

namespace canshift::boards {

constexpr BoardProfile kActiveBoard = {
    /* board_id   */ "example_alt",
    /* board_name */ "Example Alternate Board",
    /* lcd */
    {
        /* driver                */ LcdDriver::ST7789,
        /* pin_mosi              */ 23,
        /* pin_miso              */ -1,
        /* pin_sclk              */ 18,
        /* pin_cs                */ 5,
        /* pin_dc                */ 16,
        /* pin_rst               */ 4,
        /* pin_bl                */ 22,
        /* freq_write_hz         */ 40000000UL,
        /* panel_width           */ 240,
        /* panel_height          */ 320,
        /* memory_width          */ 240,
        /* memory_height         */ 320,
        /* default_rotation      */ 0,
        /* rgb_order_bgr         */ false,
        /* invert                */ true,
        /* bus_shared_with_touch */ false,
        /* readable              */ false,
        /* color_depth           */ 16,
    },
    /* backlight */
    {
        /* present       */ true,
        /* pwm_channel   */ 0,
        /* pwm_freq_hz   */ 5000,
        /* default_duty  */ 200,
        /* invert        */ false,
    },
    /* touch */
    {
        /* driver             */ TouchDriver::FT6336,
        /* pin_cs             */ -1,
        /* pin_irq            */ 21,
        /* freq_hz            */ 400000UL,
        /* needs_calibration  */ false,
    },
    /* can */
    {
        /* controller         */ CanController::EspTwai,
        /* pin_tx             */ 25,
        /* pin_rx             */ 26,
        /* default_speed_kbps */ 500,
    },
    /* storage */
    {
        /* spiffs_present  */ true,
        /* spiffs_size_kb  */ 1024,
        /* sd_present      */ false,
        /* sd_pin_cs       */ -1,
    },
    /* conn */
    {
        /* wifi_supported */ true,
        /* ble_supported  */ true,
        /* psram_present  */ true,
    },
};

} // namespace canshift::boards

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI _bus;
    // lgfx::Touch_FT5x06 _touch;  // pick the right touch class
    lgfx::Light_PWM _light;

public:
    LGFX() {
        // Mirror the structure used in boards/crowpanel_28.h:
        //  - configure _bus with the board's SPI host + frequencies + pins
        //  - configure _panel with size, offsets, invert, bus_shared
        //  - configure _light with backlight pin + PWM channel
        //  - configure _touch (if present) and call _panel.setTouch(&_touch)
        //  - setPanel(&_panel)
    }
};

#endif // -- commented-out reference template -------------------------------
