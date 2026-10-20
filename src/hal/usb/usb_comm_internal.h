#pragma once

#include "usb_comm.h"

#include "config/typed_config_body.h"

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace UsbCommInternal {

extern char *s_rxBuf;

extern volatile uint32_t s_lastHostCmdMs;

void handleCommand(char *jsonLine);

void routeLine(const char *line, size_t len);

[[nodiscard]] bool replyReachesSerial();

[[nodiscard]] bool replyReachesSocket();

[[nodiscard]] bool commandFromSocket();

void rebootIntoSavedConfig();

void commitSignalsUpload();

[[nodiscard]] bool takeSocketClosed();

void sendToSocket(const char *line, size_t len);

void dispatchFromSerial(char *jsonLine);

void sendTypedConfigGet(const char *path, const char *fieldKey, TypedConfigBody::BodyOf bodyOf);
void handlePutDeviceConfig(const JsonObjectConst &obj);
void handlePutInputBindings(const JsonObjectConst &obj);

void handleOtaBegin(const JsonObjectConst &obj);
void handleOtaWriteRaw(char *jsonLine);
void handleOtaEnd(const JsonObjectConst &obj);

struct DecodeScratch {
    char *buf;
    size_t cap;
};

void handlePutFile(const JsonObjectConst &obj, const DecodeScratch &scratch);
void handlePutConfig(const char *jsonLine);

void handlePutConfigChunk(const JsonObjectConst &obj);
void abortSocketConfigChunk(const char *reason);
[[nodiscard]] bool socketConfigChunkOpen();
void tickConfigChunkTimeout();
void handleGetConfig();
void abortChunkTransfer(const char *reason);

void handleCanScanStart();
void handleCanScanStop();

void handleBusScanStart();
void handleBusScanPoll();

void handleObdReadDtc();
void handleObdClearDtc();
void tickObdDtc();

constexpr size_t kTypedPutMaxPayloadBytes = 8192;

bool canScanModeActive();
bool canScanQueueTrySend(const UsbComm::CanScanFrame &frame);
bool canScanQueueTryReceive(UsbComm::CanScanFrame &out);

void tickChunkTransferTimeout();
void tickOtaSessionWatch(bool socketClosed);

void abortSocketTransfers(const char *reason);

[[nodiscard]] bool socketFileTransferOpen();
[[nodiscard]] bool socketOtaSessionOpen();
void publishSocketTransfer(bool inFlight);

void invokeBurnOverlayShow();
void invokeBurnOverlayShowError(int reason);

}
