#pragma once
// board_config.h — Hardware pin assignments for Elecrow CrowPanel 2.8" ESP32
//
// !! CRITICAL !!
// Every pin assignment here is an ASSUMPTION based on typical ESP32 TFT modules.
// You MUST verify these against your actual board schematic or silk screen
// before flashing. Incorrect assignments may damage the display or MCU.
//
// Board: Elecrow CrowPanel 2.8" ESP32 HMI
// Display IC: ILI9341 (320x240, SPI)
// Touch IC: XPT2046 (resistive, SPI — shared bus)
// MCU: ESP32-WROOM-32
//
// Pinout verified against official Elecrow documentation (SKU DIS05028H):
//   https://www.elecrow.com/wiki/esp32-display-282727-intelligent-touch-screen-wi-fi26ble-240320-hmi-display.html
//   https://github.com/Elecrow-RD/CrowPanel-2.8-ESP32-HMI-320x240
//
// TWAI (CAN) is wired to the expansion-header pins GPIO 25/32 (issue #237).
// GPIO 21/22 (I2C header) and GPIO 16/17 (UART2) are intentionally left free
// for future expansion.

// ---------------------------------------------------------------------------
// Display — ILI9341 (SPI)
// Shared SPI bus with touch controller
// ---------------------------------------------------------------------------

#define PIN_TFT_MOSI 13
#define PIN_TFT_MISO 12 // Often not used (display is write-only)
#define PIN_TFT_SCLK 14
#define PIN_TFT_CS 15
#define PIN_TFT_DC 2   // Data/Command (RS)
#define PIN_TFT_RST -1 // Not connected on CrowPanel 2.8" (held high internally)
#define PIN_TFT_BL 27  // Backlight PWM — 0=off, 255=full

// SPI clock speeds — verified from official CrowPanel 2.8" documentation.
// HW_TFT_FAST_SPI is an opt-in flag for boards that have been validated to
// run the panel at 40 MHz reliably (issue #95, fix F6). Default OFF — leave
// it that way until you have measured no flicker, no flush errors, and no
// missed touch frames over a full driving session on the real CrowPanel.
// When in doubt keep it at the official 27 MHz.
#ifndef HW_TFT_FAST_SPI
    #define HW_TFT_FAST_SPI 0
#endif
#if HW_TFT_FAST_SPI
    #define TFT_SPI_FREQ_HZ 40000000UL // 40 MHz — opt-in, verify on real board
#else
    #define TFT_SPI_FREQ_HZ 27000000UL // 27 MHz — official spec for this board
#endif
#define TOUCH_SPI_FREQ_HZ 2500000UL // 2.5 MHz — XPT2046 max

// ---------------------------------------------------------------------------
// Touch — XPT2046 (resistive, SPI)
// ---------------------------------------------------------------------------

#define PIN_TOUCH_CS 33
// Touch IRQ not exposed / not used — driver uses polling via getTouch()
#define PIN_TOUCH_IRQ -1

// Touch calibration is owned by LovyanGFX + NVS (see TouchDriver::calibrate()).
// No board-level defaults are needed — first boot prompts an interactive run.

// ---------------------------------------------------------------------------
// CAN Bus — ESP32 TWAI controller + Adafruit CAN Pal (TJA1051T/3)
//
// Wiring:
//   CAN Pal CTX  → ESP32 TWAI_TX (GPIO below)
//   CAN Pal CRX  → ESP32 TWAI_RX (GPIO below)
//   CAN Pal CANH → ECU CAN H
//   CAN Pal CANL → ECU CAN L
//   CAN Pal VCC  → 5V rail
//   CAN Pal GND  → GND
//
// NOTE: ESP32 TWAI is 3.3V logic. The CAN Pal TJA1051T/3 is 5V tolerant
//       on the CAN side and 3.3V compatible on the logic side. Verify VCC.
//
// CAUTION: Avoid GPIO 6-11 (internal flash SPI) and GPIO 34-39 (input only).
//          TWAI_TX must be a bi-directional GPIO.
//
// CrowPanel 2.8" expansion-header pinout:
//   GPIO 25 + GPIO 32 → free expansion pins (used here for TWAI)
//   GPIO 21 / 22      → I2C header (ID21-SCL / ID22-SDA) — kept free
//   GPIO 16 / 17      → UART2 (IO16-RXD2 / IO17-TXD2)
// GPIO 25 and 32 chosen because they are bi-directional, outside the flash-SPI
// range (6-11), outside the input-only range (34-39), strapping-safe at boot,
// and physically broken out on the user's CrowPanel expansion header.
// ---------------------------------------------------------------------------

#define PIN_TWAI_TX 25
#define PIN_TWAI_RX 32

// CAN bus speed — must match your ECU's CAN output configuration
// Common values: 500 kbps, 1 Mbps — verify in your ECU software
#define CAN_SPEED_KBPS 500

// ---------------------------------------------------------------------------
// Storage — SPIFFS only
//
// Configuration, fonts, and assets live on the on-chip SPIFFS partition
// (board_build.filesystem = spiffs in platformio.ini). Image is uploaded via
// `pio run -t uploadfs` from canshift-firmware/data/.
//
// SPI bus: none — SPIFFS is on on-chip flash, not SD. LCD + touch own HSPI
// exclusively (see lgfx_panel.h `bus_shared = true`).
// ---------------------------------------------------------------------------

// Config file paths on the filesystem
#define CONFIG_PATH_DASHBOARD "/config/dashboard.json"
#define CONFIG_PATH_SIGNALS "/config/signals.json"
#define CONFIG_PATH_DEVICE "/config/device.json"
#define CONFIG_PATH_ASSETS_DIR "/assets/"

// ---------------------------------------------------------------------------
// USB Serial (Phase 1 config sync)
// On ESP32, USB serial is via the UART0 bridge (GPIO 1 = TX, GPIO 3 = RX).
// This is the same UART used for flashing and Serial monitor.
// In Phase 1, config sync uses UART0 at a distinct baud rate.
//
// TODO: Consider using UART2 on different pins if UART0 conflict is an issue.
// ---------------------------------------------------------------------------
#define USB_SERIAL_BAUD 115200

// ---------------------------------------------------------------------------
// Backlight PWM (driven by LovyanGFX — see lgfx_panel.h)
// ---------------------------------------------------------------------------
#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ_HZ 5000
#define BL_DEFAULT_DUTY 200 // Default brightness (0-255)
