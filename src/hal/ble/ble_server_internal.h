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

void updateStatus();

void emitTelemetry();

void refreshTimerStateValue();

void emitTimerSync();

} // namespace BleServerInternal

#endif
