#pragma once

#include "app_config.h"

#if APP_BLE_ENABLED

    #include <NimBLEDevice.h>
    #include <stddef.h>

namespace BleServerInternal {

extern NimBLECharacteristic *s_pTele;
extern NimBLECharacteristic *s_pStatus;
extern NimBLECharacteristic *s_pTimerState;
extern NimBLECharacteristic *s_pTimerLap;

extern bool s_connected;
extern bool s_stackInited;
extern bool s_gattInited;
extern bool s_enabled;
extern bool s_released;

void updateStatus();

void emitTelemetry();

void refreshTimerStateValue();

void emitTimerSync();

void noteConnected();

bool linkAdmitted();

void dropUnauthenticatedLink(const ble_gap_conn_desc &desc, const char *reason);

void dropOverduePeer();

} // namespace BleServerInternal

#endif
