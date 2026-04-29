// User_Setup.h — TFT_eSPI driver configuration for Elecrow CrowPanel 2.8"
//
// TFT_eSPI reads this file when USER_SETUP_LOADED is NOT set, or when explicitly
// included. Pin values must match board_config.h — update both together.
//
// !! CRITICAL !! — all values are ASSUMED. Verify against actual board schematic.
// See include/board_config.h for source-of-truth pin documentation.

// ---------------------------------------------------------------------------
// Display IC
// ---------------------------------------------------------------------------
#define ILI9341_DRIVER

// Physical dimensions (portrait orientation, before rotation)
// HW_DISPLAY_ROTATION=1 (landscape) is applied in software.
#define TFT_WIDTH 240
#define TFT_HEIGHT 320

// ---------------------------------------------------------------------------
// SPI pin assignments — must match board_config.h
// ---------------------------------------------------------------------------
// TODO: Verify against CrowPanel 2.8" schematic before first flash
#define TFT_MOSI 13 // PIN_TFT_MOSI
#define TFT_MISO 12 // PIN_TFT_MISO
#define TFT_SCLK 14 // PIN_TFT_SCLK
#define TFT_CS 15   // PIN_TFT_CS
#define TFT_DC 2    // PIN_TFT_DC
#define TFT_RST 4   // PIN_TFT_RST
#define TFT_BL 27   // PIN_TFT_BL — backlight PWM, driven in display_driver.cpp

// Touch CS intentionally NOT defined here:
// XPT2046 is driven by touch_driver.cpp using its own SPI transaction,
// not by TFT_eSPI's built-in touch support.
// #define TOUCH_CS  33  // PIN_TOUCH_CS

// ---------------------------------------------------------------------------
// SPI frequencies
// ---------------------------------------------------------------------------
#define SPI_FREQUENCY 40000000      // 40 MHz — reduce if display artifacts appear
#define SPI_READ_FREQUENCY 20000000 // 20 MHz — for readbacks (unused in normal operation)
#define SPI_TOUCH_FREQUENCY 2500000 // 2.5 MHz — XPT2046 maximum

// ---------------------------------------------------------------------------
// Font loading — keep only fonts used in the UI to save flash
// ---------------------------------------------------------------------------
#define LOAD_GLCD   // Built-in Glcd font (font 1)
#define LOAD_FONT2  // Small font (font 2)
#define LOAD_FONT4  // Medium font (font 4)
#define SMOOTH_FONT // Anti-aliased fonts via SPIFFS (unused for now)
