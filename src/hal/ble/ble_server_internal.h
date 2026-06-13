#pragma once

#include "app_config.h"

#if APP_BLE_ENABLED

    #include <NimBLEDevice.h>
    #include <stddef.h>

namespace BleServerInternal {

extern NimBLECharacteristic *s_pTele;
extern NimBLECharacteristic *s_pStatus;

extern bool s_connected;

void updateStatus();

void emitTelemetry();

} // namespace BleServerInternal

#endif
