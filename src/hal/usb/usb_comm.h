#pragma once

#include "app_config.h"

#include <stdint.h>
#include <stddef.h>

namespace UsbComm {

void reserveRxBuf();

void init();

void tick();

static constexpr uint8_t kCmdGetConfig = 0x01;
static constexpr uint8_t kCmdPutConfig = 0x02;

static constexpr uint8_t kCmdGetDeviceConfig = 0x03;

static constexpr uint8_t kCmdPutDeviceConfig = 0x04;

static constexpr uint8_t kCmdScreenSettings = 0x05;

static constexpr uint8_t kCmdPutFile = 0x06;

static constexpr uint8_t kCmdToggleDayNight = 0x07;

static constexpr uint8_t kCmdCalibrateTouch = 0x08;

static constexpr uint8_t kCmdSetDayNight = 0x09;

static constexpr uint8_t kCmdResetTouchCal = 0x0A;

static constexpr uint8_t kCmdGetInputBindings = 0x0B;

static constexpr uint8_t kCmdPutInputBindings = 0x0C;
static constexpr uint8_t kCmdSetBoardProfile = 0x0D;
static constexpr uint8_t kCmdPutConfigChunk = 0x0E;
static constexpr uint8_t kCmdGetStatus = 0x10;
static constexpr uint8_t kCmdPing = 0x11;
static constexpr uint8_t kCmdCanScanStart = 0x20;
static constexpr uint8_t kCmdCanScanStop = 0x21;
static constexpr uint8_t kCmdObdReadDtc = 0x22;
static constexpr uint8_t kCmdObdClearDtc = 0x23;

static constexpr uint8_t kCmdOtaBegin = 0x30;
static constexpr uint8_t kCmdOtaWrite = 0x31;
static constexpr uint8_t kCmdOtaEnd = 0x32;

static constexpr uint8_t kCmdReboot = 0xF0;

struct CanScanFrame {
    uint32_t id;
    uint8_t len;
    uint8_t data[8];
};

bool pushCanFrame(const CanScanFrame &frame);

void updateCanStats(uint32_t fpsX10, uint32_t errors, uint32_t missed);

bool isHostActive();

void sendLine(const char *line);

void sendOk();

void sendOkRebooting();

void flushAndRestart();

void sendError(const char *code);

using LineSink = void (*)(const char *line, size_t len);

#if APP_WORKSHOP_ENABLED

void setSocketSink(LineSink sink);

void dispatchFromSocket(char *jsonLine);

void noteSocketClosed();

#endif

using BurnOverlayShowCb = void (*)();

using BurnOverlayShowErrorCb = void (*)(int reason);

void setBurnOverlayShowCallback(BurnOverlayShowCb cb);
void setBurnOverlayShowErrorCallback(BurnOverlayShowErrorCb cb);

} // namespace UsbComm
