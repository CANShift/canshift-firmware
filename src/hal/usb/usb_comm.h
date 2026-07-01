#pragma once

#include <stdint.h>
#include <stddef.h>

namespace UsbComm {

void reserveRxBuf();

void init();

void tick();

static constexpr uint8_t CMD_GET_CONFIG = 0x01;
static constexpr uint8_t CMD_PUT_CONFIG = 0x02;

static constexpr uint8_t CMD_GET_DEVICE_CONFIG = 0x03;

static constexpr uint8_t CMD_PUT_DEVICE_CONFIG = 0x04;

static constexpr uint8_t CMD_SCREEN_SETTINGS = 0x05;

static constexpr uint8_t CMD_PUT_FILE = 0x06;

static constexpr uint8_t CMD_TOGGLE_DAY_NIGHT = 0x07;

static constexpr uint8_t CMD_CALIBRATE_TOUCH = 0x08;

static constexpr uint8_t CMD_SET_DAY_NIGHT = 0x09;

static constexpr uint8_t CMD_RESET_TOUCH_CAL = 0x0A;

static constexpr uint8_t CMD_GET_INPUT_BINDINGS = 0x0B;

static constexpr uint8_t CMD_PUT_INPUT_BINDINGS = 0x0C;
static constexpr uint8_t CMD_GET_STATUS = 0x10;
static constexpr uint8_t CMD_PING = 0x11;
static constexpr uint8_t CMD_CAN_SCAN_START = 0x20;
static constexpr uint8_t CMD_CAN_SCAN_STOP = 0x21;

static constexpr uint8_t CMD_OTA_BEGIN = 0x30;
static constexpr uint8_t CMD_OTA_WRITE = 0x31;
static constexpr uint8_t CMD_OTA_END = 0x32;

static constexpr uint8_t CMD_REBOOT = 0xF0;

struct CanScanFrame {
    uint32_t id;
    uint8_t len;
    uint8_t data[8];
};

bool pushCanFrame(const CanScanFrame &frame);

void updateCanStats(uint32_t fpsX10, uint32_t errors);

bool isHostActive();

void sendLine(const char *line);

using BurnOverlayShowCb = void (*)();

using BurnOverlayShowErrorCb = void (*)(int reason);

void setBurnOverlayShowCallback(BurnOverlayShowCb cb);
void setBurnOverlayShowErrorCallback(BurnOverlayShowErrorCb cb);

} // namespace UsbComm
