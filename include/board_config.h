#pragma once

#define PIN_TFT_MOSI 13
#define PIN_TFT_MISO 12
#define PIN_TFT_SCLK 14
#define PIN_TFT_CS 15
#define PIN_TFT_DC 2
#define PIN_TFT_RST (-1)
#define PIN_TFT_BL 27

#ifndef HW_TFT_FAST_SPI
    #define HW_TFT_FAST_SPI 0
#endif
#if HW_TFT_FAST_SPI
    #define TFT_SPI_FREQ_HZ 40000000UL
#else
    #define TFT_SPI_FREQ_HZ 27000000UL
#endif
#define TOUCH_SPI_FREQ_HZ 2500000UL

#define PIN_TOUCH_CS 33
#define PIN_TOUCH_IRQ (-1)

#define PIN_TWAI_TX 25
#define PIN_TWAI_RX 32

#define CAN_SPEED_KBPS 500

#define CONFIG_PATH_DASHBOARD "/config/dashboard.json"
#define CONFIG_PATH_SIGNALS "/config/signals.json"
#define CONFIG_PATH_DEVICE "/config/device.json"
#define CONFIG_PATH_INPUTS "/config/input_bindings.json"
#define CONFIG_PATH_ASSETS_DIR "/assets/"

#define USB_SERIAL_BAUD 115200

#define BL_PWM_CHANNEL 0
#define BL_PWM_FREQ_HZ 5000
#define BL_DEFAULT_DUTY 200
