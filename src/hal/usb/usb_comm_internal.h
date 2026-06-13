#pragma once

#include "usb_comm.h"

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace UsbCommInternal {

extern char *s_rxBuf;

extern volatile uint32_t s_lastHostCmdMs;

void handleCommand(const char *jsonLine);

void sendTypedConfigGet(const char *path, const char *fieldKey, const char *unwrapKey);
void handlePutDeviceConfig(const JsonObjectConst &obj);
void handlePutInputBindings(const JsonObjectConst &obj);

constexpr size_t kTypedPutMaxPayloadBytes = 8192;

bool canScanModeActive();
bool canScanQueueTrySend(const UsbComm::CanScanFrame &frame);
bool canScanQueueTryReceive(UsbComm::CanScanFrame &out);

void tickChunkTransferTimeout();

void invokeBurnOverlayShow();
void invokeBurnOverlayShowError(int reason);

} // namespace UsbCommInternal
