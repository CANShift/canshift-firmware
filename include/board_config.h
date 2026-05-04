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
// TWAI (CAN) uses GPIO 21/22 which are also the board's I2C header pins.
// No on-board I2C devices — safe to repurpose for external CAN transceiver.

// ---------------------------------------------------------------------------
// Display — ILI9341 (SPI)
// Shared SPI bus with touch controller
// ---------------------------------------------------------------------------

#define PIN_TFT_MOSI 13
#define PIN_TFT_MISO 12 // Often not used (display is write-only)
#define PIN_TFT_SCLK 14
#define PIN_TFT_CS 15
#define PIN_TFT_DC 2  // Data/Command (RS)
#define PIN_TFT_RST -1 // Not connected on CrowPanel 2.8" (held high internally)
#define PIN_TFT_BL 27  // Backlight PWM — 0=off, 255=full

// SPI clock speeds — verified from official CrowPanel 2.8" documentation
#define TFT_SPI_FREQ_HZ 27000000UL  // 27 MHz — official spec for this board
#define TOUCH_SPI_FREQ_HZ 2500000UL // 2.5 MHz — XPT2046 max

// ---------------------------------------------------------------------------
// Touch — XPT2046 (resistive, SPI)
// ---------------------------------------------------------------------------

#define PIN_TOUCH_CS 33
// Touch IRQ not exposed / not used — driver uses polling via getTouch()
#define PIN_TOUCH_IRQ -1

// Touch calibration (raw ADC values → screen coordinates)
// These WILL need calibration on the real board.
// TODO: Run touch calibration routine and replace these values.
#define TOUCH_CAL_X_MIN 300
#define TOUCH_CAL_X_MAX 3800
#define TOUCH_CAL_Y_MIN 300
#define TOUCH_CAL_Y_MAX 3800
#define TOUCH_SWAP_XY 0  // Set 1 if X/Y axes are swapped
#define TOUCH_INVERT_X 0 // Set 1 if X axis is mirrored
#define TOUCH_INVERT_Y 0 // Set 1 if Y axis is mirrored

// ---------------------------------------------------------------------------
// CAN Bus — ESP32 TWAI controller + Adafruit CAN Pal (TJA1051T/3)
//
// Wiring:
//   CAN Pal CTX  → ESP32 TWAI_TX (GPIO below)
//   CAN Pal CRX  → ESP32 TWAI_RX (GPIO below)
//   CAN Pal CANH → MaxxECU CAN H
//   CAN Pal CANL → MaxxECU CAN L
//   CAN Pal VCC  → 5V rail
//   CAN Pal GND  → GND
//
// NOTE: ESP32 TWAI is 3.3V logic. The CAN Pal TJA1051T/3 is 5V tolerant
//       on the CAN side and 3.3V compatible on the logic side. Verify VCC.
//
// CAUTION: Avoid GPIO 6-11 (internal flash SPI) and GPIO 34-39 (input only).
//          TWAI_TX must be a bi-directional GPIO.
// ---------------------------------------------------------------------------

// TODO: Choose GPIO pins that do not conflict with SPI or other peripherals
//       These assignments assume SPI uses GPIO 12-15 and TWAI uses 21/22.
#define PIN_TWAI_TX 22
#define PIN_TWAI_RX 21

// CAN bus speed — must match MaxxECU CAN output configuration
// MaxxECU Street default is typically 500 kbps or 1 Mbps
// TODO: Confirm MaxxECU CAN baud rate setting
#define CAN_SPEED_KBPS 500

// ---------------------------------------------------------------------------
// SD Card — microSD slot on second SPI bus (confirmed present on CrowPanel 2.8")
// ---------------------------------------------------------------------------

#define PIN_SD_PRESENT 1 // 0 = no SD, 1 = SD present

#if PIN_SD_PRESENT
    #define PIN_SD_MOSI 23
    #define PIN_SD_MISO 19
    #define PIN_SD_SCLK 18
    #define PIN_SD_CS 5
#endif

// ---------------------------------------------------------------------------
// Storage selection
// Use SPIFFS if no SD card is present.
// ---------------------------------------------------------------------------
#if PIN_SD_PRESENT
    #define STORAGE_USE_SD 1
    #define STORAGE_USE_SPIFFS 0
#else
    #define STORAGE_USE_SD 0
    #define STORAGE_USE_SPIFFS 1
#endif

// Config file paths on the filesystem
#define CONFIG_PATH_DASHBOARD "/config/dashboard.json"
#define CONFIG_PATH_SIGNALS "/config/signals.json"
#define CONFIG_PATH_DEVICE "/config/device.json"
#define CONFIG_PATH_THEME "/config/theme.json"
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
// Sentinel framing bytes for USB protocol packets
#define USB_PKT_START_BYTE 0xAA
#define USB_PKT_END_BYTE 0x55

// ---------------------------------------------------------------------------
// Backlight PWM
// ---------------------------------------------------------------------------
#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ_HZ 5000
#define BL_PWM_BITS 8       // 8-bit: 0-255
#define BL_DEFAULT_DUTY 200 // Default brightness (0-255)

// ---------------------------------------------------------------------------
// Status LED (if present)
// TODO: Check if CrowPanel has a user-controllable LED
// ---------------------------------------------------------------------------
#define PIN_LED_BUILTIN 2 // Standard ESP32 dev board LED — may not apply
#define PIN_LED_PRESENT 0 // Set 1 if board has a usable status LED
